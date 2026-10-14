// SPDX-License-Identifier: BSD-2-Clause

// Copyright (c) 2019-2020, Paul Ferrand, Andrea Zanellato
// All rights reserved.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:

// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.

// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "FilePool.h"
#include "AudioReader.h"
#include "Buffer.h"
#include "AudioBuffer.h"
#include "AudioSpan.h"
#include "Config.h"
#include "utility/SwapAndPop.h"
#include "utility/Debug.h"
#include <ThreadPool.h>
#include <absl/types/span.h>
#include <absl/strings/match.h>
#include <absl/memory/memory.h>
#include <algorithm>
#include <memory>
#include <thread>
#include <system_error>
#include <atomic>
#include <chrono>
#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif
using namespace std::placeholders;

static std::weak_ptr<ThreadPool> globalThreadPoolWeakPtr;
static std::mutex globalThreadPoolMutex;

static std::shared_ptr<ThreadPool> globalThreadPool()
{
    std::shared_ptr<ThreadPool> threadPool;

    threadPool = globalThreadPoolWeakPtr.lock();
    if (threadPool)
        return threadPool;

    std::lock_guard<std::mutex> lock(globalThreadPoolMutex);
    threadPool = globalThreadPoolWeakPtr.lock();
    if (threadPool)
        return threadPool;

    unsigned numThreads = std::thread::hardware_concurrency();
    numThreads = (numThreads > 2) ? (numThreads - 2) : 1;
    threadPool.reset(new ThreadPool(numThreads));
    globalThreadPoolWeakPtr = threadPool;
    return threadPool;
}

void readBaseFile(sfz::AudioReader& reader, sfz::FileAudioBuffer& output, uint32_t numFrames)
{
    output.reset();
    output.resize(numFrames);

    const unsigned channels = reader.channels();

    if (channels == 1) {
        output.addChannel();
        output.clear();
        reader.readNextBlock(output.channelWriter(0), numFrames);
    } else if (channels == 2) {
        output.addChannel();
        output.addChannel();
        output.clear();
        sfz::Buffer<float> tempReadBuffer { 2 * numFrames };
        reader.readNextBlock(tempReadBuffer.data(), numFrames);
        sfz::readInterleaved(tempReadBuffer, output.getSpan(0), output.getSpan(1));
    }
}

sfz::FileAudioBuffer readFromFile(sfz::AudioReader& reader, uint32_t numFrames)
{
    sfz::FileAudioBuffer baseBuffer;
    readBaseFile(reader, baseBuffer, numFrames);
    return baseBuffer;
}

void streamFromFile(sfz::AudioReader& reader, sfz::FileAudioBuffer& output, std::atomic<size_t>* filledFrames = nullptr)
{
    const auto numFrames = static_cast<size_t>(reader.frames());
    const auto numChannels = reader.channels();
    const auto chunkSize = static_cast<size_t>(sfz::config::fileChunkSize);

    output.reset();
    output.addChannels(reader.channels());
    output.resize(numFrames);
    output.clear();

    sfz::Buffer<float> fileBlock { chunkSize * numChannels };
    size_t inputFrameCounter { 0 };
    size_t outputFrameCounter { 0 };
    bool inputEof = false;

    while (!inputEof && inputFrameCounter < numFrames)
    {
        auto thisChunkSize = std::min(chunkSize, numFrames - inputFrameCounter);
        const auto numFramesRead = static_cast<size_t>(
            reader.readNextBlock(fileBlock.data(), thisChunkSize));
        if (numFramesRead == 0)
            break;

        if (numFramesRead < thisChunkSize) {
            inputEof = true;
            thisChunkSize = numFramesRead;
        }
        const auto outputChunkSize = thisChunkSize;

        for (size_t chanIdx = 0; chanIdx < numChannels; chanIdx++) {
            const auto outputChunk = output.getSpan(chanIdx).subspan(outputFrameCounter, outputChunkSize);
            for (size_t i = 0; i < thisChunkSize; ++i)
                outputChunk[i] = fileBlock[i * numChannels + chanIdx];
        }
        inputFrameCounter += thisChunkSize;
        outputFrameCounter += outputChunkSize;

        if (filledFrames != nullptr)
            filledFrames->fetch_add(outputChunkSize);
    }
}

sfz::FilePool::FilePool()
    : filesToLoad(alignedNew<FileQueue>()),
      threadPool(globalThreadPool())
{
    gentleThread = std::thread(&FilePool::gentleThreadJob, this);   // [sfizz for MPC OS]
    loadingJobs.reserve(config::maxVoices);
    lastUsedFiles.reserve(config::maxVoices);
    garbageToCollect.reserve(config::maxVoices);
}

sfz::FilePool::~FilePool()
{
    std::error_code ec;

    gentleRun = false;   // [sfizz for MPC OS]
    if (gentleThread.joinable())
        gentleThread.join();

    garbageFlag = false;
    semGarbageBarrier.post(ec);
    garbageThread.join();

    dispatchFlag = false;
    dispatchBarrier.post(ec);
    dispatchThread.join();

    std::lock_guard<std::mutex> guard { loadingJobsMutex };   // [sfizz for MPC OS] gentle streams requeue under it
    for (auto& job : loadingJobs)
        job.wait();
}

bool sfz::FilePool::checkSample(std::string& filename) const noexcept
{
    fs::path path { rootDirectory / filename };
    std::error_code ec;
    if (fs::exists(path, ec))
        return true;

#if defined(_WIN32)
    return false;
#else
    fs::path oldPath = std::move(path);
    path = oldPath.root_path();

    static const fs::path dot { "." };
    static const fs::path dotdot { ".." };

    for (const fs::path& part : oldPath.relative_path()) {
        if (part == dot || part == dotdot) {
            path /= part;
            continue;
        }

        if (fs::exists(path / part, ec)) {
            path /= part;
            continue;
        }

        auto it = path.empty() ? fs::directory_iterator { dot, ec } : fs::directory_iterator { path, ec };
        if (ec) {
            DBG("Error creating a directory iterator for " << filename << " (Error code: " << ec.message() << ")");
            return false;
        }

        auto searchPredicate = [&part](const fs::directory_entry &ent) -> bool {
#if !defined(GHC_USE_WCHAR_T)
            return absl::EqualsIgnoreCase(
                ent.path().filename().native(), part.native());
#else
            return absl::EqualsIgnoreCase(
                ent.path().filename().u8string(), part.u8string());
#endif
        };

        while (it != fs::directory_iterator {} && !searchPredicate(*it))
            it.increment(ec);

        if (it == fs::directory_iterator {}) {
            DBG("File not found, could not resolve " << filename);
            return false;
        }

        path /= it->path().filename();
    }

    const auto newPath = fs::relative(path, rootDirectory, ec);
    if (ec) {
        DBG("Error extracting the new relative path for " << filename << " (Error code: " << ec.message() << ")");
        return false;
    }
    DBG("Updating " << filename << " to " << newPath);
    filename = newPath.string();
    return true;
#endif
}

bool sfz::FilePool::checkSampleId(FileId& fileId) const noexcept
{
    if (loadedFiles.contains(fileId))
        return true;

    std::string filename = fileId.filename();
    bool result = checkSample(filename);
    if (result)
        fileId = FileId(std::move(filename), fileId.isReverse());
    return result;
}

absl::optional<sfz::FileInformation> getReaderInformation(sfz::AudioReader* reader) noexcept
{
    const unsigned channels = reader->channels();
    if (channels != 1 && channels != 2)
        return {};

    sfz::FileInformation returnedValue;
    returnedValue.end = static_cast<uint32_t>(reader->frames()) - 1;
    returnedValue.sampleRate = static_cast<double>(reader->sampleRate());
    returnedValue.numChannels = static_cast<int>(channels);

    // Check for instrument info
    sfz::InstrumentInfo instrumentInfo {};
    if (reader->getInstrumentInfo(instrumentInfo)) {
        returnedValue.rootKey = clamp<uint8_t>(instrumentInfo.basenote, 0, 127);
        if (reader->type() == sfz::AudioReaderType::Forward) {
            if (instrumentInfo.loop_count > 0) {
                returnedValue.hasLoop = true;
                returnedValue.loopStart = instrumentInfo.loops[0].start;
                returnedValue.loopEnd =
                    min(returnedValue.end, static_cast<int64_t>(instrumentInfo.loops[0].end - 1));
            }
        } else {
            // TODO loops ignored when reversed
            //   prehaps it can make use of SF_LOOP_BACKWARD?
        }
    }

    // Check for wavetable info
    sfz::WavetableInfo wt {};
    if (reader->getWavetableInfo(wt))
        returnedValue.wavetable = wt;

    return returnedValue;
}

absl::optional<sfz::FileInformation> sfz::FilePool::checkExistingFileInformation(const FileId& fileId) noexcept
{
    const auto loadedFile = loadedFiles.find(fileId);
    if (loadedFile != loadedFiles.end())
        return loadedFile->second.information;

    const auto preloadedFile = preloadedFiles.find(fileId);
    if (preloadedFile != preloadedFiles.end())
        return preloadedFile->second.information;

    return {};
}

absl::optional<sfz::FileInformation> sfz::FilePool::getFileInformation(const FileId& fileId) noexcept
{
    auto existingInformation = checkExistingFileInformation(fileId);
    if (existingInformation)
        return existingInformation;

    const fs::path file { rootDirectory / fileId.filename() };

    if (!fs::exists(file))
        return {};

    AudioReaderPtr reader = createAudioReader(file, fileId.isReverse());
    return getReaderInformation(reader.get());
}

bool sfz::FilePool::preloadFile(const FileId& fileId, uint32_t maxOffset) noexcept
{
    const auto loadedFile = loadedFiles.find(fileId);
    if (loadedFile != loadedFiles.end()) {
        loadedFile->second.preloadCallCount++;
        return true;
    }

    auto fileInformation = getFileInformation(fileId);
    if (!fileInformation)
        return false;

    fileInformation->maxOffset = maxOffset;
    const fs::path file { rootDirectory / fileId.filename() };
    AudioReaderPtr reader = createAudioReader(file, fileId.isReverse());

    const auto frames = static_cast<uint32_t>(reader->frames());
    const auto framesToLoad = [&]() {
        if (loadInRam)
            return frames;
        else
            return min(frames, maxOffset + preloadSize);
    }();

    const auto existingFile = preloadedFiles.find(fileId);
    if (existingFile != preloadedFiles.end()) {
        if (framesToLoad > existingFile->second.preloadedData.getNumFrames()) {
            preloadedFiles[fileId].information.maxOffset = maxOffset;
            preloadedFiles[fileId].preloadedData = readFromFile(*reader, framesToLoad);
        }
        existingFile->second.preloadCallCount++;
    } else {
        fileInformation->sampleRate = static_cast<double>(reader->sampleRate());
        auto insertedPair = preloadedFiles.insert_or_assign(fileId, {
            readFromFile(*reader, framesToLoad),
            *fileInformation
        });

        insertedPair.first->second.status = FileData::Status::Preloaded;
        insertedPair.first->second.preloadCallCount++;
    }

    return true;
}

void sfz::FilePool::resetPreloadCallCounts() noexcept
{
    for (auto& preloadedFile: preloadedFiles)
        preloadedFile.second.preloadCallCount = 0;

    for (auto& loadedFile: loadedFiles)
        loadedFile.second.preloadCallCount = 0;
}

// [sfizz for MPC OS] Upstream relied on file ids expiring. Gentle streams outlive a note and point at the file records, so
// before records are cleared or removed they are told to stop (generation) and waited for. Not for the audio thread.
void sfz::FilePool::emptyFileLoadingQueues() noexcept
{
    streamGeneration.fetch_add(1);
    std::lock_guard<std::mutex> guard { loadingJobsMutex };
    for (auto& job : loadingJobs)
        job.wait();
    loadingJobs.clear();
    std::lock_guard<std::mutex> busy { gentleWorkMutex };
    std::lock_guard<std::mutex> list { gentleListMutex };
    gentleStreams.clear();
}

void sfz::FilePool::removeUnusedPreloadedData() noexcept
{
    emptyFileLoadingQueues();
    for (auto it = preloadedFiles.begin(), end = preloadedFiles.end(); it != end; ) {
        auto copyIt = it++;
        if (copyIt->second.preloadCallCount == 0) {
            DBG("[sfizz] Removing unused preloaded data: " << copyIt->first.filename());
            preloadedFiles.erase(copyIt);
        }
    }

    for (auto it = loadedFiles.begin(), end = loadedFiles.end(); it != end; ) {
        auto copyIt = it++;
        if (copyIt->second.preloadCallCount == 0) {
            DBG("[sfizz] Removing unused loaded data: " << copyIt->first.filename());
            loadedFiles.erase(copyIt);
        }
    }
}

sfz::FileDataHolder sfz::FilePool::loadFile(const FileId& fileId) noexcept
{
    auto fileInformation = getFileInformation(fileId);
    if (!fileInformation)
        return {};

    const auto existingFile = loadedFiles.find(fileId);
    if (existingFile != loadedFiles.end()) {
        existingFile->second.preloadCallCount++;
        return { &existingFile->second };
    }

    const fs::path file { rootDirectory / fileId.filename() };
    AudioReaderPtr reader = createAudioReader(file, fileId.isReverse());

    const auto frames = static_cast<uint32_t>(reader->frames());
    auto insertedPair = loadedFiles.insert_or_assign(fileId, {
        readFromFile(*reader, frames),
        *fileInformation
    });
    insertedPair.first->second.status = FileData::Status::Preloaded;
    insertedPair.first->second.preloadCallCount++;
    return { &insertedPair.first->second };
}

sfz::FileDataHolder sfz::FilePool::loadFromRam(const FileId& fileId, const std::vector<char>& data) noexcept
{
    const auto loaded = loadedFiles.find(fileId);
    if (loaded != loadedFiles.end())
        return { &loaded->second };

    auto reader = createAudioReaderFromMemory(data.data(), data.size(), fileId.isReverse());
    auto fileInformation = getReaderInformation(reader.get());
    const auto frames = static_cast<uint32_t>(reader->frames());
    auto insertedPair = loadedFiles.insert_or_assign(fileId, {
        readFromFile(*reader, frames),
        *fileInformation
    });
    insertedPair.first->second.status = FileData::Status::Preloaded;
    insertedPair.first->second.preloadCallCount++;
    DBG("Added a file " << fileId.filename());
    return { &insertedPair.first->second };
}

sfz::FileDataHolder sfz::FilePool::getFilePromise(const std::shared_ptr<FileId>& fileId) noexcept
{
    const auto loaded = loadedFiles.find(*fileId);
    if (loaded != loadedFiles.end())
        return { &loaded->second };

    const auto preloaded = preloadedFiles.find(*fileId);
    if (preloaded == preloadedFiles.end()) {
        DBG("[sfizz] File not found in the preloaded files: " << fileId->filename());
        return {};
    }

    if (!loadInRam) {
        QueuedFileData queuedData { fileId, &preloaded->second };
        if (!filesToLoad->try_push(queuedData)) {
            DBG("[sfizz] Could not enqueue the file to load for " << fileId << " (queue capacity " << filesToLoad->capacity() << ")");
            return {};
        }

        std::error_code ec;
        dispatchBarrier.post(ec);
        ASSERT(!ec);
    }

    return { &preloaded->second };
}

void sfz::FilePool::setPreloadSize(uint32_t preloadSize) noexcept
{
    this->preloadSize = preloadSize;
    if (loadInRam)
        return;

    // Update all the preloaded sizes
    for (auto& preloadedFile : preloadedFiles) {
        const auto maxOffset = preloadedFile.second.information.maxOffset;
        fs::path file { rootDirectory / preloadedFile.first.filename() };
        AudioReaderPtr reader = createAudioReader(file, preloadedFile.first.isReverse());
        preloadedFile.second.preloadedData = readFromFile(*reader, preloadSize + maxOffset);
    }
}

// ---- [sfizz for MPC OS] gentle loading -------------------------------------------------------------------------------
static std::atomic<bool> gGentleLoading { false };
constexpr double kGentleAheadSeconds = 1.0;   // decoded ahead of the start at once (the preload covers the first moments)
constexpr double kGentleRate = 2.0;           // then at most this many times real time (enough for a note an octave up)
constexpr int kGentleTickMs = 15;            // the streaming thread's round over all playing files

void sfz::FilePool::setGentleLoading(bool gentle) noexcept { gGentleLoading = gentle; }

struct sfz::FilePool::GentleStream {
    std::weak_ptr<FileId> id;
    FileData* data { nullptr };
    fs::path file;
    bool reverse { false };
    AudioReaderPtr reader;
    size_t numFrames { 0 };
    unsigned numChannels { 0 };
    size_t out { 0 };
    double rate { 48000.0 };
    unsigned generation { 0 };
    std::chrono::steady_clock::time_point started;
    sfz::Buffer<float> block { static_cast<size_t>(sfz::config::fileChunkSize) * 2 };
    bool draining { false };   // stopped: voices read the preload again; free once that's sure
    std::chrono::steady_clock::time_point drainSince;

    bool open() // (re)starts from the beginning; the output buffer is sized but not cleared: voices read only decoded frames
    {
        std::error_code ec;
        reader = createAudioReader(file, reverse, &ec);
        if (ec)
            return false;
        numFrames = static_cast<size_t>(reader->frames());
        numChannels = reader->channels();
        rate = reader->sampleRate() > 0 ? reader->sampleRate() : 48000.0;
        block.resize(static_cast<size_t>(sfz::config::fileChunkSize) * numChannels);
        out = 0;
        started = std::chrono::steady_clock::now();
        if (data->fileData.getNumFrames() != numFrames || data->fileData.getNumChannels() != numChannels) {
            data->fileData.reset();
            data->fileData.addChannels(numChannels);
            data->fileData.resize(numFrames);
        }
        return true;
    }
};

// One step for one file, never blocking: decode up to the allowed point, or finish, or stop when nobody plays it.
sfz::FilePool::GentleResult sfz::FilePool::gentleStep(GentleStream& st) noexcept
{
    if (!dispatchFlag || streamGeneration.load() != st.generation || st.id.expired())
        return GentleResult::Finished;   // clearing or closing: the file pool resets these files itself
    FileData& fd = *st.data;
    const auto now = std::chrono::steady_clock::now();

    if (st.draining) {
        if (fd.readerCount.load() > 0) {          // a note started meanwhile: start over (it plays the preload until then)
            st.draining = false;
            return st.open() ? GentleResult::Continue : GentleResult::Finished;
        }
        if (now - st.drainSince < std::chrono::milliseconds(20))
            return GentleResult::Continue;        // until no render block can still be reading the old frames
        fd.fileData.reset();
        fd.status = FileData::Status::Preloaded;
        auto expected = FileData::Status::Preloaded;   // a note that started between the checks saw "Streaming": take it over
        if (fd.readerCount.load() > 0 && fd.status.compare_exchange_strong(expected, FileData::Status::Streaming)) {
            st.draining = false;
            return st.open() ? GentleResult::Continue : GentleResult::Finished;
        }
        return GentleResult::Finished;
    }

    if (fd.readerCount.load() <= 0) {
        // No voice plays the file any more: back to the preloaded start (as triggerGarbageCollection() does for idle files)
        fd.availableFrames = 0;
        st.draining = true;
        st.drainSince = now;
        return GentleResult::Continue;
    }

    const size_t chunk = static_cast<size_t>(config::fileChunkSize);
    const double elapsed = std::chrono::duration<double>(now - st.started).count();
    const double allowed = st.rate * (kGentleAheadSeconds + kGentleRate * elapsed);
    while (st.out < st.numFrames && double(st.out) < allowed) {
        const size_t want = std::min(chunk, st.numFrames - st.out);
        const size_t got = static_cast<size_t>(st.reader->readNextBlock(st.block.data(), want));
        if (got == 0) {
            st.numFrames = st.out;
            break;
        }
        for (unsigned c = 0; c < st.numChannels; ++c) {
            const auto dst = fd.fileData.getSpan(c).subspan(st.out, got);
            for (size_t i = 0; i < got; ++i)
                dst[i] = st.block[i * st.numChannels + c];
        }
        st.out += got;
        fd.availableFrames.fetch_add(got);
    }

    if (st.out >= st.numFrames) {
        fd.status = FileData::Status::Done;
        std::shared_ptr<FileId> id = st.id.lock();
        if (id) {
            std::lock_guard<SpinMutex> guard { garbageAndLastUsedMutex };
            if (absl::c_find(lastUsedFiles, *id) == lastUsedFiles.end())
                lastUsedFiles.push_back(*id);
        }
        return GentleResult::Finished;
    }
    return GentleResult::Continue;
}

void sfz::FilePool::gentleThreadJob() noexcept
{
    std::vector<std::shared_ptr<GentleStream>> work;
    while (gentleRun.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kGentleTickMs));
        std::lock_guard<std::mutex> busy { gentleWorkMutex };
        {
            std::lock_guard<std::mutex> list { gentleListMutex };
            work.swap(gentleStreams);
        }
        if (work.empty())
            continue;
        std::vector<std::shared_ptr<GentleStream>> keep;
        for (auto& st : work)
            if (gentleStep(*st) == GentleResult::Continue)
                keep.push_back(st);
        work.clear();
        std::lock_guard<std::mutex> list { gentleListMutex };
        for (auto& st : keep)
            if (st->generation == streamGeneration.load())
                gentleStreams.push_back(std::move(st));
    }
}

void sfz::FilePool::loadingJob(const QueuedFileData& data) noexcept
{
    if (gGentleLoading.load()) {
#if !defined(_WIN32)
        // ordinary scheduling: the device's real-time audio threads come first (workers may have been raised before)
        sched_param param {};
        param.sched_priority = 0;
        pthread_setschedparam(pthread_self(), SCHED_OTHER, &param);
#endif
    } else {
        raiseCurrentThreadPriority();
    }

    std::shared_ptr<FileId> id = data.id.lock();
    if (!id) {
        // file ID was nulled, it means the region was deleted, ignore
        return;
    }

    const fs::path file { rootDirectory / id->filename() };
    std::error_code readError;
    AudioReaderPtr reader = createAudioReader(file, id->isReverse(), &readError);

    if (readError) {
        DBG("[sfizz] reading the file errored for " << *id << " with code " << readError << ": " << readError.message());
        return;
    }

    FileData::Status currentStatus = data.data->status.load();

    unsigned spinCounter { 0 };
    while (currentStatus == FileData::Status::Invalid) {
        // Spin until the state changes
        if (spinCounter > 1024) {
            DBG("[sfizz] " << *id << " is stuck on Invalid? Leaving the load");
            return;
        }

        std::this_thread::sleep_for(std::chrono::microseconds(100));
        currentStatus = data.data->status.load();
        spinCounter += 1;
    }

    // Already loading or loaded
    if (currentStatus != FileData::Status::Preloaded)
        return;

    // Someone else got the token
    if (!data.data->status.compare_exchange_strong(currentStatus, FileData::Status::Streaming))
        return;

    if (gGentleLoading.load()) {   // [sfizz for MPC OS]
        auto st = std::make_shared<GentleStream>();
        st->id = data.id;
        st->data = data.data;
        st->file = file;
        st->reverse = id->isReverse();
        st->generation = streamGeneration.load();
        if (!st->open())
            return;
        if (gentleStep(*st) == GentleResult::Continue) {   // the first seconds now; the streaming thread does the rest
            std::lock_guard<std::mutex> list { gentleListMutex };
            if (st->generation == streamGeneration.load())
                gentleStreams.push_back(std::move(st));
        }
        return;
    }

    streamFromFile(*reader, data.data->fileData, &data.data->availableFrames);

    data.data->status = FileData::Status::Done;

    std::lock_guard<SpinMutex> guard { garbageAndLastUsedMutex };
    if (absl::c_find(lastUsedFiles, *id) == lastUsedFiles.end())
        lastUsedFiles.push_back(*id);
}

void sfz::FilePool::clear()
{
    std::lock_guard<SpinMutex> guard { garbageAndLastUsedMutex };
    emptyFileLoadingQueues();
    garbageToCollect.clear();
    lastUsedFiles.clear();
    preloadedFiles.clear();
    loadedFiles.clear();
}

uint32_t sfz::FilePool::getPreloadSize() const noexcept
{
    return preloadSize;
}

template <typename R>
bool is_ready(std::future<R> const& f)
{
    return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

void sfz::FilePool::dispatchingJob() noexcept
{
    while (dispatchBarrier.wait(), dispatchFlag) {
        std::lock_guard<std::mutex> guard { loadingJobsMutex };

        QueuedFileData queuedData;
        if (filesToLoad->try_pop(queuedData)) {
            if (queuedData.id.expired()) {
                // file ID was nulled, it means the region was deleted, ignore
            }
            else
                loadingJobs.push_back(
                    threadPool->enqueue([this](const QueuedFileData& data) { loadingJob(data); }, std::move(queuedData)));
        }

        // Clear finished jobs
        swapAndPopAll(loadingJobs, [](std::future<void>& future) {
            return is_ready(future);
        });
    }
}

void sfz::FilePool::garbageJob() noexcept
{
    while (semGarbageBarrier.wait(), garbageFlag) {
        std::lock_guard<SpinMutex> guard { garbageAndLastUsedMutex };
        garbageToCollect.clear();
    }
}

void sfz::FilePool::waitForBackgroundLoading() noexcept
{
    std::lock_guard<std::mutex> guard { loadingJobsMutex };

    for (auto& job : loadingJobs)
        job.wait();

    loadingJobs.clear();
}

void sfz::FilePool::raiseCurrentThreadPriority() noexcept
{
#if defined(_WIN32)
    HANDLE thread = GetCurrentThread();
    const int priority = THREAD_PRIORITY_ABOVE_NORMAL; /*THREAD_PRIORITY_HIGHEST*/
    if (!SetThreadPriority(thread, priority)) {
        std::system_error error(GetLastError(), std::system_category());
        DBG("[sfizz] Cannot set current thread priority: " << error.what());
    }
#else
    pthread_t thread = pthread_self();
    int policy;
    sched_param param;

    if (pthread_getschedparam(thread, &policy, &param) != 0) {
        DBG("[sfizz] Cannot get current thread scheduling parameters");
        return;
    }

    policy = SCHED_RR;
    const int minprio = sched_get_priority_min(policy);
    const int maxprio = sched_get_priority_max(policy);
    param.sched_priority = minprio + config::backgroundLoaderPthreadPriority * (maxprio - minprio) / 100;

    if (pthread_setschedparam(thread, policy, &param) != 0) {
        DBG("[sfizz] Cannot set current thread scheduling parameters");
        return;
    }
#endif
}

void sfz::FilePool::setRamLoading(bool loadInRam) noexcept
{
    if (loadInRam == this->loadInRam)
        return;

    this->loadInRam = loadInRam;

    if (loadInRam) {
        for (auto& preloadedFile : preloadedFiles) {
            fs::path file { rootDirectory / preloadedFile.first.filename() };
            AudioReaderPtr reader = createAudioReader(file, preloadedFile.first.isReverse());
            preloadedFile.second.preloadedData = readFromFile(
                *reader,
                preloadedFile.second.information.end
            );
        }
    } else {
        setPreloadSize(preloadSize);
    }
}

void sfz::FilePool::triggerGarbageCollection() noexcept
{
    const std::unique_lock<SpinMutex> guard { garbageAndLastUsedMutex, std::try_to_lock };
    if (!guard.owns_lock())
        return;

    const auto now = std::chrono::high_resolution_clock::now();
    swapAndPopAll(lastUsedFiles, [&](const FileId& id) {
        if (garbageToCollect.size() == garbageToCollect.capacity())
           return false;

        auto it = preloadedFiles.find(id);
        if (it == preloadedFiles.end()) {
            // Getting here means that the preloadedFiles got changed (probably cleared)
            // while the lastUsedFiles were untouched.
            return true;
        }

        sfz::FileData& data = it->second;
        if (data.status == FileData::Status::Preloaded)
            return true;

        if (data.status != FileData::Status::Done)
            return false;

        if (data.readerCount != 0)
            return false;

        const auto secondsIdle = std::chrono::duration_cast<std::chrono::seconds>(now - data.lastViewerLeftAt).count();
        if (secondsIdle < config::fileClearingPeriod)
            return false;

        data.availableFrames = 0;
        data.status = FileData::Status::Preloaded;
        garbageToCollect.push_back(std::move(data.fileData));
        return true;
    });

    std::error_code ec;
    semGarbageBarrier.post(ec);
    ASSERT(!ec);
}

// [sfizz for MPC OS] C hook for the engine adapter (src/sfizz_mpc.cpp): 1 = gentle loading, 0 = upstream.
extern "C" void sfizz_mpc_set_gentle_loading(int gentle) { sfz::FilePool::setGentleLoading(gentle != 0); }

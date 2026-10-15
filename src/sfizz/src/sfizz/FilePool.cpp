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
#include <algorithm>
#include <new>
#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif
using namespace std::placeholders;

// [sfizz for MPC OS] bounded streaming settings and counters (see the end of the file)
namespace {
std::atomic<bool> gGentleLoading { false };
std::atomic<size_t> gStreamBytes { 0 };                    // decoded sample memory in use, all file pools
std::atomic<size_t> gStreamBudget { size_t(192) << 20 };   // set by the host from the device's free memory
std::atomic<unsigned> gStreamCount { 0 };
std::atomic<size_t> gReserved { 0 };                       // address space reserved by streams (whole decoded files)
// address space for whole decoded files: plenty for idle files to stay cached, well inside a 32-bit process
size_t reserveLimit() { return std::min<size_t>(std::max<size_t>(gStreamBudget.load() * 3, size_t(256) << 20), size_t(768) << 20); }
std::atomic<unsigned> gStarved { 0 };                      // decode steps refused by the budget
std::atomic<unsigned> gFailed { 0 };                       // allocations that failed
std::atomic<unsigned> gTaken { 0 };                        // playing files freed for newer ones (memory short)
std::atomic<unsigned> gUnderruns { 0 };                    // notes cut because their data wasn't decoded in time
constexpr double kUrgentSeconds = 0.35;   // first pass: every playing file this far ahead of its furthest reader
constexpr double kAheadSeconds = 1.25;    // second pass: this far ahead
constexpr int kTickMs = 8;                // the streamer's round
constexpr int kWaitMs = 150;              // a new file waits this long for memory (its preloaded start plays meanwhile)
constexpr int kIdleSeconds = sfz::config::fileClearingPeriod;   // an unused file's memory is kept this long
}

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
try {
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
} catch (const std::bad_alloc&) {   // [sfizz for MPC OS] out of memory: an empty buffer (a silent note), not an abort
    output.reset();
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
    streamThread = std::thread(&FilePool::streamerJob, this);   // [sfizz for MPC OS]
    loadingJobs.reserve(config::maxVoices);
    lastUsedFiles.reserve(config::maxVoices);
    garbageToCollect.reserve(config::maxVoices);
}

sfz::FilePool::~FilePool()
{
    std::error_code ec;

    {   // [sfizz for MPC OS]
        std::lock_guard<std::mutex> lock { streamMutex };
        streamRun = false;
    }
    streamWake.notify_all();
    if (streamThread.joinable())
        streamThread.join();

    garbageFlag = false;
    semGarbageBarrier.post(ec);
    garbageThread.join();

    dispatchFlag = false;
    dispatchBarrier.post(ec);
    dispatchThread.join();

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

void sfz::FilePool::removeUnusedPreloadedData() noexcept
{
    waitForBackgroundLoading();   // [sfizz for MPC OS] entries are erased below
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
    waitForBackgroundLoading();   // [sfizz for MPC OS] the preloads are rewritten below
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

void sfz::FilePool::loadingJob(const QueuedFileData& data) noexcept
{
    if (gGentleLoading.load()) {   // [sfizz for MPC OS] claim the file, then let the streamer do the work
        if (data.id.expired())
            return;
        auto expected = FileData::Status::Preloaded;
        if (!data.data->status.compare_exchange_strong(expected, FileData::Status::Streaming))
            return;   // already streaming, done, or not ready
        {
            std::lock_guard<std::mutex> lock { streamMutex };
            streamRequests.push_back(data);
        }
        streamWake.notify_one();
        return;
    }

    raiseCurrentThreadPriority();

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
    {
        std::lock_guard<std::mutex> guard { loadingJobsMutex };

        for (auto& job : loadingJobs)
            job.wait();

        loadingJobs.clear();
    }
    stopAllStreams();   // [sfizz for MPC OS] files are about to be cleared or moved: no stream may outlive this
}

void sfz::FilePool::emptyFileLoadingQueues() noexcept   // [sfizz for MPC OS] was a no-op
{
    waitForBackgroundLoading();
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


// =====================================================================================================================
// [sfizz for MPC OS] Bounded streaming
// =====================================================================================================================

struct sfz::FilePool::GentleStream {
    std::weak_ptr<FileId> id;
    FileData* data { nullptr };
    AudioReaderPtr reader;
    size_t numFrames { 0 };
    unsigned numChannels { 0 };
    size_t out { 0 };          // frames decoded into data->fileData
    double rate { 48000.0 };
    size_t bytes { 0 };        // charged to the budget
    size_t reserved { 0 };     // address space held (the whole decoded file)
    bool idle { false };
    std::chrono::steady_clock::time_point idleSince;
    std::chrono::steady_clock::time_point began;   // when its data started: the oldest playing file yields first
    bool retiring { false };                       // its voices are fading out; freed once they have
    bool waiting { false };                        // requested, its memory not reserved yet
    std::chrono::steady_clock::time_point requestedAt;
    std::chrono::steady_clock::time_point retiredAt;
    sfz::Buffer<float> block;
};

void sfz::FilePool::setGentleLoading(bool gentle) noexcept { gGentleLoading = gentle; }
void sfz::FilePool::setStreamBudget(size_t bytes) noexcept { gStreamBudget = bytes; }
void sfz::FilePool::noteUnderrun() noexcept { gUnderruns++; }
sfz::FilePool::StreamStats sfz::FilePool::streamStats() noexcept
{
    return { gStreamBytes.load(), gStreamBudget.load(), gStreamCount.load(), gStarved.load(), gFailed.load() };
}

// Open the file (once) and reserve the whole decoded size. The pages are only committed as frames are written (the
// buffer is a fresh mapping), so memory follows what was decoded, not the file's length. `Wait`: the address space
// for it isn't free yet (older files are being retired to make room); try again on the next tick.
sfz::FilePool::BeginResult sfz::FilePool::streamBegin(GentleStream& st) noexcept
{
    std::shared_ptr<FileId> id = st.id.lock();
    if (!id)
        return BeginResult::Failed;
    try {
        if (!st.reader) {
            std::error_code ec;
            st.reader = createAudioReader(rootDirectory / id->filename(), id->isReverse(), &ec);
            if (ec || !st.reader)
                return BeginResult::Failed;
            st.numFrames = static_cast<size_t>(st.reader->frames());
            st.numChannels = st.reader->channels();
            st.rate = st.reader->sampleRate() > 0 ? st.reader->sampleRate() : 48000.0;
            if (st.numChannels < 1 || st.numChannels > 2 || st.numFrames == 0)
                return BeginResult::Failed;
            if (!st.block.resize(static_cast<size_t>(config::fileChunkSize) * st.numChannels, std::nothrow))
                return BeginResult::Failed;
        }
        const size_t reserve = st.numFrames * st.numChannels * sizeof(float);
        if (gReserved.load() + reserve > reserveLimit() && !streamEvictIdle(0, &st, reserve)) {
            streamTakeFromOldest(0, reserve, st);   // too many files open for a 32-bit process: retire the oldest
            gStarved++;
            return BeginResult::Wait;
        }
        FileAudioBuffer& fd = st.data->fileData;
        fd.reset();
        fd.addChannels(st.numChannels);
        if (!fd.resize(st.numFrames, std::nothrow)) {
            fd.reset();
            gFailed++;
            return BeginResult::Failed;
        }
        st.reserved = reserve;
        gReserved += reserve;
    } catch (const std::exception&) {   // bad_alloc and the like: never let it reach the host
        st.data->fileData.reset();
        gFailed++;
        return BeginResult::Failed;
    }
    st.out = 0;
    st.bytes = 0;
    st.idle = false;
    st.waiting = false;
    st.retiring = false;
    st.began = std::chrono::steady_clock::now();
    st.data->availableFrames = 0;
    return BeginResult::Ok;
}

// After freeing a file that voices still read (a note started on it meanwhile): stream it again. Returns false when
// the stream object is done with (its file plays its preloaded start; a later note can stream it again).
bool sfz::FilePool::streamClaimAgain(GentleStream& st) noexcept
{
    if (st.data->readerCount.load() > 0) {
        auto expected = FileData::Status::Preloaded;
        if (st.data->status.compare_exchange_strong(expected, FileData::Status::Streaming)) {
            st.reader.reset();   // reopen: decoding starts over from the file's start
            const BeginResult r = streamBegin(st);
            if (r == BeginResult::Ok)
                return true;
            if (r == BeginResult::Wait) {
                st.waiting = true;
                st.requestedAt = std::chrono::steady_clock::now();
                st.began = st.requestedAt;
                return true;
            }
            st.data->fileData.reset();
            st.data->availableFrames = 0;
            st.data->status = FileData::Status::Preloaded;
        }
    }
    return false;
}

// Decode up to `target` frames, within the budget. Returns false when the budget stopped it. With `mayTakeFromOlder`,
// a file that needs memory to keep playing takes it from the file that has been playing longest, as voice stealing
// would: when memory runs short, the oldest notes end (quickly faded) and new notes still sound in full.
bool sfz::FilePool::streamDecodeTo(GentleStream& st, size_t target, bool mayTakeFromOlder) noexcept
{
    target = std::min(target, st.numFrames);
    const size_t chunk = static_cast<size_t>(config::fileChunkSize);
    while (st.out < target) {
        const size_t want = std::min(chunk, st.numFrames - st.out);
        const size_t cost = want * st.numChannels * sizeof(float);
        if (gStreamBytes.load() + cost > gStreamBudget.load() && !streamEvictIdle(cost, &st)
) {
            if (mayTakeFromOlder)
                streamTakeFromOldest(cost, 0, st);
            gStarved++;
            return false;
        }
        const size_t got = static_cast<size_t>(st.reader->readNextBlock(st.block.data(), want));
        if (got == 0) {          // shorter than announced: end here
            st.numFrames = st.out;
            break;
        }
        FileAudioBuffer& fd = st.data->fileData;
        for (unsigned c = 0; c < st.numChannels; ++c) {
            float* dst = fd.getSpan(c).data() + st.out;
            for (size_t i = 0; i < got; ++i)
                dst[i] = st.block[i * st.numChannels + c];
        }
        const size_t gotBytes = got * st.numChannels * sizeof(float);
        st.bytes += gotBytes;
        gStreamBytes += gotBytes;
        st.out += got;
        st.data->availableFrames.store(st.out);   // publish after the frames are written
    }
    if (st.out >= st.numFrames)
        st.data->status = FileData::Status::Done;
    return true;
}

// Give a file's decoded memory back. Voices then read the preloaded start again (as upstream's garbage collector
// leaves an idle file). If a render block may be reading the data right now, wait until it has finished.
void sfz::FilePool::streamFree(GentleStream& st, bool readersMayExist) noexcept
{
    FileData& fd = *st.data;
    fd.availableFrames.store(0);
    if (readersMayExist) {
        const unsigned epoch = renderEpoch.load();
        if (epoch & 1) {
            for (int i = 0; i < 2000 && renderEpoch.load() == epoch; ++i)   // at most ~2 s; one block takes ~3 ms
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    fd.fileData.reset();
    gStreamBytes -= st.bytes;
    gReserved -= st.reserved;
    st.bytes = 0;
    st.reserved = 0;
    st.out = 0;
    st.reader.reset();
    fd.readHead = 0;
    fd.status = FileData::Status::Preloaded;
}

// Free idle files, the longest idle first, until `needBytes` more fit in the budget.
bool sfz::FilePool::streamEvictIdle(size_t needBytes, const GentleStream* keep, size_t needReserve) noexcept
{
    for (;;) {
        if (gStreamBytes.load() + needBytes <= gStreamBudget.load() && gReserved.load() + needReserve <= reserveLimit())
            return true;
        GentleStream* oldest = nullptr;
        for (auto& s : streams)
            if (s.get() != keep && s->data && s->idle && !s->waiting && s->reserved > 0 && s->data->readerCount.load() == 0
                && (!oldest || s->idleSince < oldest->idleSince))
                oldest = s.get();
        if (!oldest)
            return false;
        streamFree(*oldest, true);
        if (!streamClaimAgain(*oldest))
            oldest->data = nullptr;   // removed at the end of the tick
    }
}

struct sfz::FilePool::Playing {
    GentleStream* st;
    size_t head;      // frames: the furthest reader, at least the preloaded start
    double margin;    // decoded frames ahead of it (negative: behind)
};

// Make room by retiring the files that have played longest (and began before `forStream`): their voices fade out over
// a few milliseconds, then the next ticks free them, so this always leaves the caller waiting a little (its lookahead,
// or for a new file its preloaded start, covers the wait).
void sfz::FilePool::streamTakeFromOldest(size_t needBytes, size_t needReserve, const GentleStream& forStream) noexcept
{
    size_t retiringBytes = 0, retiringReserve = 0;
    for (auto& s : streams)
        if (s->data && s->retiring) {
            retiringBytes += s->bytes;
            retiringReserve += s->reserved;
        }
    const auto short_ = [&] {
        const size_t bytes = gStreamBytes.load(), reserved = gReserved.load();
        return bytes - std::min(retiringBytes, bytes) + needBytes > gStreamBudget.load()
            || reserved - std::min(retiringReserve, reserved) + needReserve > reserveLimit();
    };
    while (short_()) {
        GentleStream* oldest = nullptr;
        for (auto& s : streams)
            if (s.get() != &forStream && s->data && !s->retiring && !s->waiting && s->reserved > 0
                && s->began < forStream.began && (!oldest || s->began < oldest->began))
                oldest = s.get();
        if (!oldest)
            return;
        oldest->retiring = true;
        oldest->retiredAt = std::chrono::steady_clock::now();
        oldest->data->retireEpoch.fetch_add(1);
        retiringBytes += oldest->bytes;
        retiringReserve += oldest->reserved;
        gTaken++;
    }
}

// Free retiring files whose voices have faded (or after a short grace). Their files stream again on a new note.
void sfz::FilePool::streamFreeRetired() noexcept
{
    const auto now = std::chrono::steady_clock::now();
    for (auto& s : streams) {
        if (!s->data || !s->retiring)
            continue;
        if (s->data->readerCount.load() > 0 && now - s->retiredAt < std::chrono::milliseconds(80))
            continue;
        streamFree(*s, true);
        s->retiring = false;
        if (!streamClaimAgain(*s))   // a note that started on it meanwhile keeps it streaming
            s->data = nullptr;       // removed at the end of the tick
    }
}

void sfz::FilePool::streamerTick() noexcept
{
    streamFreeRetired();

    // new files wait in `streams` until they have their memory
    const auto now = std::chrono::steady_clock::now();
    for (auto& request : streamRequests) {
        auto st = absl::make_unique<GentleStream>();
        st->id = request.id;
        st->data = request.data;
        st->waiting = true;
        st->requestedAt = now;
        st->began = now;   // the newest: older files retire for it, not the other way round
        streams.push_back(std::move(st));
    }
    streamRequests.clear();

    for (auto& st : streams) {
        if (!st->data)
            continue;
        if (st->id.expired()) {   // its region is gone (the instrument changed)
            st->data = nullptr;
            continue;
        }
        if (st->waiting) {
            const bool gaveUp = st->data->readerCount.load() == 0 || now - st->requestedAt > std::chrono::milliseconds(kWaitMs);
            const BeginResult r = gaveUp ? BeginResult::Failed : streamBegin(*st);
            if (r == BeginResult::Ok || r == BeginResult::Wait)
                continue;
            st->reader.reset();
            st->data->fileData.reset();
            st->data->availableFrames = 0;
            st->data->status = FileData::Status::Preloaded;   // its notes play their preloaded start
            st->data = nullptr;
            continue;
        }
        if (st->retiring)
            continue;
        const bool readers = st->data->readerCount.load() > 0;
        if (readers)
            st->idle = false;
        else if (!st->idle) {
            st->idle = true;
            st->idleSince = now;
        } else if (now - st->idleSince > std::chrono::seconds(kIdleSeconds)) {
            streamFree(*st, true);
            if (!streamClaimAgain(*st))
                st->data = nullptr;
        }
    }

    // two passes: everyone a little ahead first, the most starved first; then the full lookahead.
    // Each stream's read position is read once: voices move it while this runs, and sorting on live values would
    // give std::sort an inconsistent order (it then reads past the array's ends).
    std::vector<Playing>& playing = playingScratch;
    playing.clear();
    for (auto& st : streams)
        if (st->data && !st->idle && !st->retiring && !st->waiting && st->out < st->numFrames) {
            const size_t head = std::max<size_t>(st->data->readHead.load(), st->data->preloadedData.getNumFrames());
            playing.push_back({ st.get(), head, double(st->out) - double(head) });
        }
    std::sort(playing.begin(), playing.end(), [](const Playing& a, const Playing& b) { return a.margin < b.margin; });
    for (auto& p : playing)   // (a file freed to make room for another has data == nullptr from then on)
        if (p.st->data)
            streamDecodeTo(*p.st, p.head + static_cast<size_t>(kUrgentSeconds * p.st->rate), true);
    for (auto& p : playing)
        if (p.st->data)
            streamDecodeTo(*p.st, p.head + static_cast<size_t>(kAheadSeconds * p.st->rate), false);

    streams.erase(std::remove_if(streams.begin(), streams.end(),
                                 [](const std::unique_ptr<GentleStream>& s) { return s->data == nullptr; }),
                  streams.end());
}

void sfz::FilePool::streamerJob() noexcept
{
    // A normal thread, as upstream's loaders: a real-time one would run ahead of the MPC's own non-real-time work
    // (its disk streaming, the screen) for as long as a burst of decoding lasts. Real-time audio threads are ahead of
    // it either way, and the lookahead (over a second) absorbs scheduling delays.
    std::unique_lock<std::mutex> lock { streamMutex };
    while (streamRun) {
        streamWake.wait_for(lock, std::chrono::milliseconds(kTickMs));
        if (!streamRun)
            break;
        streamerTick();
        gStreamCount = static_cast<unsigned>(streams.size());
    }
}

// Called while nothing renders (loading, clearing, preload changes): drop every stream and reset its file.
void sfz::FilePool::stopAllStreams() noexcept
{
    std::lock_guard<std::mutex> lock { streamMutex };
    for (auto& st : streams)
        if (st->data && !st->id.expired())
            streamFree(*st, false);
        else if (st->data) {   // its file is gone: only the accounting is left
            gStreamBytes -= st->bytes;
            gReserved -= st->reserved;
        }
    streams.clear();
    for (auto& request : streamRequests)
        if (!request.id.expired())
            request.data->status = FileData::Status::Preloaded;
    streamRequests.clear();
}

// C hooks for the engine adapter (src/sfizz_mpc.cpp)
extern "C" void sfizz_mpc_set_gentle_loading(int gentle) { sfz::FilePool::setGentleLoading(gentle != 0); }
extern "C" void sfizz_mpc_set_stream_budget(unsigned long long bytes) { sfz::FilePool::setStreamBudget(static_cast<size_t>(bytes)); }
extern "C" void sfizz_mpc_stream_stats(unsigned long long* bytes, unsigned long long* budget, unsigned* streams,
                                       unsigned* starved, unsigned* failed)
{
    const auto s = sfz::FilePool::streamStats();
    *bytes = s.bytes; *budget = s.budget; *streams = s.streams; *starved = s.starved; *failed = s.failed;
}
extern "C" unsigned sfizz_mpc_underruns() { return gUnderruns.load(); }
extern "C" unsigned sfizz_mpc_streams_taken() { return gTaken.load(); }

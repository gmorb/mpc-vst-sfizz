#pragma once
#include "absl/types/span.h"
#include "MathHelpers.h"

namespace sfz
{

/**
* @brief Lookup a value from the pan table
*        No check is done on the range, needs to be capped
*        between 0 and panSize.
*
* @param pan
* @return float
*/
float panLookup(float pan);


/**
 * @brief Pans a mono signal left or right
 *
 * @param panEnvelope
 * @param leftBuffer
 * @param rightBuffer
 * @param size
 */
void pan(const float* panEnvelope, float* leftBuffer, float* rightBuffer, unsigned size) noexcept;
inline void pan(absl::Span<const float> panEnvelope, absl::Span<float> leftBuffer, absl::Span<float> rightBuffer) noexcept
{
    CHECK_SPAN_SIZES(panEnvelope, leftBuffer, rightBuffer);
    pan(panEnvelope.data(), leftBuffer.data(), rightBuffer.data(), minSpanSize(panEnvelope, leftBuffer, rightBuffer));
}

/**
 * @brief [sfizz for MPC OS] A stereo voice's amplitude, pan, width, position and make-up gain in one pass, for pan,
 * width and position constant over the block. Only valid where canFuseAmpPanWidth() says the result equals the
 * separate stages (ARM NEON, aligned buffers, a multiple of 4 frames).
 */
bool canFuseAmpPanWidth(const float* gain, const float* leftBuffer, const float* rightBuffer, unsigned size) noexcept;
void ampPanWidthStereo(const float* gain, float* leftBuffer, float* rightBuffer, unsigned size, float panValue,
                       float widthValue, float positionValue, float postGain) noexcept;

/**
 * @brief Controls the width of a stereo signal, setting it to mono when width = 0 and inverting the channels
 * when width = -1. Width = 1 has no effect.
 *
 * @param widthEnvelope
 * @param leftBuffer
 * @param rightBuffer
 * @param size
 */
void width(const float* widthEnvelope, float* leftBuffer, float* rightBuffer, unsigned size) noexcept;
inline void width(absl::Span<const float> widthEnvelope, absl::Span<float> leftBuffer, absl::Span<float> rightBuffer) noexcept
{
    CHECK_SPAN_SIZES(widthEnvelope, leftBuffer, rightBuffer);
    width(widthEnvelope.data(), leftBuffer.data(), rightBuffer.data(), minSpanSize(widthEnvelope, leftBuffer, rightBuffer));
}

}

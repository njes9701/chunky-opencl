#include "rt.h"
#include "constants.h"

float computeDiffuseProbability(float4 color, bool fancierTranslucency) {
    if (fancierTranslucency) {
        float maxComponent = fmax(color.x, fmax(color.y, color.z));
        return 1.0f - pow(1.0f - color.w, maxComponent);
    }
    return color.w;
}

float computeAbsorption(float4 color, float pDiffuse, bool fancierTranslucency) {
    if (fancierTranslucency) {
        return 1.0f - (1.0f - color.w) / (1.0f - pDiffuse + EPS);
    }
    return color.w;
}

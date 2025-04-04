#include "./CraigTypes.h"

#ifdef CRAIG_INTERPOLATION

namespace MiniCraig {

    CraigConstruction operator|(const CraigConstruction& first, const CraigConstruction& second); {
        return static_cast<CraigConstruction>(static_cast<uint8_t>(first) | static_cast<uint8_t>(second));
    }

}

#endif

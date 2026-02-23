#pragma once

#include "common.h"
#include "side.h"

struct Order
{
    enum class Type
    {
        UNKNOWN = 0,
        GTC = 1,
	    FAK = 2,
	    FOK = 3,
	    GFD = 4,
	    MAR = 5
    };

    using Id = uint64_t;

    Id id{0};
    Type type{Type::UNKNOWN};
    Quantity remainder{0};
    Side side{Side::UNKNOWN};
    Price price{0};

    void fill(Quantity quantity);
    bool filled() const;
};

#pragma once

#include "common.h"

struct TradeInfo
{
    Order::Id order_id{0};
    Price price{0};
    Quantity quantity{0};
};

#pragma once

struct Change
{
    Quantity remainder{0};
    Side side{Side::UNKNOWN};
    Price price{0};   
};

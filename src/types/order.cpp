#include "order.h"

#include <stdexcept>
#include <format>

void Order::fill(Quantity quantity)
{
    if (remainder < quantity) {
        throw std::logic_error(std::format("Order ({}) cannot be filled for more than its remaining quantity.", id));
    }

    remainder -= quantity;
}

bool Order::filled() const
{
    return remainder == 0;
}

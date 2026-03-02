# Orderbook

A C++ implementation of a limit order book — a core component in trading systems and market data processing.  
This project focuses on correctness, performance, and a clear API for building matching engines, market simulators, or research tools.

## Overview

An **order book** tracks active *buy* (bid) and *sell* (ask) orders for one or more instruments, maintains price-time priority, and enables efficient matching of orders.  
This implementation aims to be clean, extensible, and suitable for both education and performance-sensitive applications.

## Supported Order Types

This order book supports **Market** orders and **Limit** orders with the following policies:

### Market
- **MAR** — Market order. Executes immediately against the best available liquidity and never rests in the book.

### Limit (with TIF)
- **GTC (Good-Till-Cancel)** — stays in the book until it is fully filled or explicitly canceled.
- **GFD (Good-For-Day)** — valid for the current trading day (typical exchange semantics).  
  *Note:* requires a session/day rollover handler to purge remaining orders.
- **FAK (Fill-And-Kill)** — executes immediately (can be partially filled); any remaining quantity is canceled.
- **FOK (Fill-Or-Kill)** — executes immediately only if the full quantity can be filled; otherwise canceled.

### Other

## Features

- Price/time priority matching  
- Efficient data structures for bid/ask levels  
- CMake build system  
- Ready to extend for market simulators or exchange engines

## How It Works

At a high level:

- Orders are stored in data structures keyed by price level and side.
- Best bid and ask are always quickly accessible.
- Matching follows standard exchange rules: trades occur when bid price ≥ ask price.
- Price-time priority is the core execution logic.

## Build Instructions

### Requirements

- C++17 (or newer) compatible compiler  
- CMake 3.15+  
- (Optional) tools for profiling & performance

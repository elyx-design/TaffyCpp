#pragma once
// Domain-operation ABIs; all native allocations remain Rust-owned.
// Status: 0 success, 1 rejected input/native error, 2 caught Rust panic.
// Grid additionally returns 3 for insufficient caller output capacity.
#include "../ported/private/support/private_suite_grid.h"
#include "../ported/private/support/private_suite_occupancy_named.h"
#include "../ported/private/support/private_suite_misc.h"

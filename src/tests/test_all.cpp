// tests/test_all.cpp
//
// Aggregates every test header into one translation unit.
//
// Premake only compiles `.cpp` files, so header-only test suites need a TU
// that includes them. Adding a new test suite means adding one `#include`
// here, otherwise its TEST() registrations never reach the registry and the
// suite silently runs zero tests.
#include "app/test_font_faces.h"
#include "app/test_frame_history.h"
#include "board/test_board_math.h"
#include "board/test_board_entities.h"
#include "board/test_image_source.h"
#include "ecs/bench_ecs.h"
#include "ecs/ecs_sample_components.h"
#include "ecs/test_ecs.h"
#include "sparse_set/bench_sparse.h"
#include "sparse_set/test_sparse.h"

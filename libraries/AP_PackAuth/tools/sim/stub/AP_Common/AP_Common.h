#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <functional>
#define CLASS_NO_COPY(c) c(const c &other) = delete; c &operator=(const c&) = delete
#define FUNCTOR_BIND_MEMBER(f, rt, ...) [this](){ (this->*f)(); }

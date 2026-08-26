#pragma once

namespace jobpilot {

enum class ErrorCode {
    OK = 0,

    CONFIG_ERROR = 1000,

    MYSQL_ERROR = 2000,

    REDIS_ERROR = 3000,

    INTERNAL_ERROR = 5000
};

}

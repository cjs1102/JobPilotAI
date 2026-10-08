#pragma once

namespace jobpilot {

enum class ErrorCode : int {
    kSuccess = 0,
    kInvalidParam = 10001,
    kMalformedRequest = 10002,
    kUnauthorized = 20001,
    kTokenExpired = 20002,
    kForbidden = 20003,
    kEmailExists = 30001,
    kUserNotFound = 30002,
    kInvalidPassword = 30003,
    kJobNotFound = 40001,
    kResumeNotFound = 40002,
    kApplicationNotFound = 40003,
    kDuplicateApplication = 40004,
    kInvalidStatusTransition = 40005,
    kAiTaskNotFound = 50001,
    kAiServiceBusy = 50002,
    kAiAnalysisFailed = 50003,
    kInternalError = 60001,
    kDatabaseError = 60002
};

inline const char *ErrorMessage(ErrorCode code)
{
    switch (code)
    {
    case ErrorCode::kSuccess:
        return "success";
    case ErrorCode::kInvalidParam:
        return "invalid parameter";
    case ErrorCode::kMalformedRequest:
        return "malformed request body";
    case ErrorCode::kUnauthorized:
        return "unauthorized";
    case ErrorCode::kTokenExpired:
        return "token expired";
    case ErrorCode::kForbidden:
        return "forbidden";
    case ErrorCode::kEmailExists:
        return "email already exists";
    case ErrorCode::kUserNotFound:
        return "user not found";
    case ErrorCode::kInvalidPassword:
        return "invalid password";
    case ErrorCode::kJobNotFound:
        return "job not found";
    case ErrorCode::kResumeNotFound:
        return "resume not found";
    case ErrorCode::kApplicationNotFound:
        return "application not found";
    case ErrorCode::kDuplicateApplication:
        return "duplicate application";
    case ErrorCode::kInvalidStatusTransition:
        return "invalid status transition";
    case ErrorCode::kAiTaskNotFound:
        return "ai task not found";
    case ErrorCode::kAiServiceBusy:
        return "ai service busy";
    case ErrorCode::kAiAnalysisFailed:
        return "ai analysis failed";
    case ErrorCode::kInternalError:
        return "internal error";
    case ErrorCode::kDatabaseError:
        return "database error";
    }

    return "internal error";
}

}

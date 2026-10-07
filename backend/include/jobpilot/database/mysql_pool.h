#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <variant>
#include <vector>

#include <mysql/mysql.h>

#include "jobpilot/database/sql_result.h"

namespace jobpilot::core
{

    // SQL 参数的最小类型集合。
    //
    // 与 03 数据库设计中的主要字段类型对应：
    //   TINYINT / INT / BIGINT          -> std::int64_t
    //   INT UNSIGNED / BIGINT UNSIGNED  -> std::uint64_t
    //   浮点数                          -> double
    //   VARCHAR / TEXT / DATETIME文本   -> std::string
    //   SQL NULL                        -> std::nullptr_t
    using SqlParam = std::variant<
        std::int64_t,
        std::uint64_t,
        double,
        std::string,
        std::nullptr_t>;

    struct ExecutionResult
    {
        std::uint64_t affectedRows = 0;
        std::uint64_t lastInsertId = 0;
    };

    class ConnectionPool
    {
    public:
        // Move-only lease of one pooled connection. A Transaction must not
        // outlive its ConnectionPool.
        class Transaction
        {
        public:
            ~Transaction();

            Transaction(const Transaction &) = delete;
            Transaction &operator=(
                const Transaction &) = delete;

            Transaction(Transaction &&other) noexcept;

            Transaction &operator=(
                Transaction &&other) noexcept;

            Result Query(
                const std::string &sql,
                const std::vector<SqlParam> &params);

            ExecutionResult Execute(
                const std::string &sql,
                const std::vector<SqlParam> &params);

            void Commit();
            void Rollback();

        private:
            friend class ConnectionPool;

            Transaction(
                ConnectionPool *pool,
                MYSQL *connection) noexcept;

            void EnsureActive() const;
            void ReleaseNoThrow() noexcept;

            ConnectionPool *pool_;
            MYSQL *connection_;
            bool active_;
        };

        ConnectionPool(
            const std::string &host,
            std::uint16_t port,
            const std::string &user,
            const std::string &password,
            const std::string &database,
            std::size_t maxConn);

        ~ConnectionPool();

        ConnectionPool(const ConnectionPool &) = delete;
        ConnectionPool &operator=(const ConnectionPool &) = delete;

        MYSQL *Get(int timeoutMs = 5000);

        void Return(MYSQL *connection);

        Result Query(
            const std::string &sql,
            const std::vector<SqlParam> &params);

        ExecutionResult Execute(
            const std::string &sql,
            const std::vector<SqlParam> &params);

        Transaction BeginTransaction();

    private:
        friend class Transaction;

        std::string host_;
        std::uint16_t port_;
        std::string user_;
        std::string password_;
        std::string database_;

        std::deque<MYSQL *> idle_;
        std::mutex mutex_;
        std::condition_variable condition_;

        std::size_t total_;
        std::size_t maxConn_;
    };

} // namespace jobpilot::core

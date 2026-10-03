#include "jobpilot/database/mysql_pool.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    // 根据 MYSQL_BIND::is_null 的实际类型推导 NULL 标志类型。
    //
    // 不直接依赖 my_bool。
    //
    // 注意：
    // 这里不能使用 std::vector<bool>，
    // 因为 vector<bool> 使用位压缩，其 operator[]
    // 返回的是代理对象，无法直接取真实 bool*。
    using MysqlNullFlag =
        std::remove_cv_t<
            std::remove_pointer_t<
                decltype(MYSQL_BIND{}.is_null)>>;

    class ParamBindContext
    {
    public:
        explicit ParamBindContext(
            const std::vector<jobpilot::core::SqlParam> &params)
            : params_(params),
              binds_(params_.size()),
              lengths_(params_.size(), 0),
              nullFlags_(
                  params_.size() == 0
                      ? nullptr
                      : std::make_unique<MysqlNullFlag[]>(
                            params_.size()))
        {
            BuildBinds();
        }

        MYSQL_BIND *data()
        {
            return binds_.data();
        }

        std::size_t size() const
        {
            return binds_.size();
        }

    private:
        void BuildBinds()
        {
            for (std::size_t i = 0;
                 i < params_.size();
                 ++i)
            {
                MYSQL_BIND &bind = binds_[i];

                bind = MYSQL_BIND{};

                std::visit(
                    [this, i, &bind](auto &value)
                    {
                        using T =
                            std::decay_t<decltype(value)>;

                        if constexpr (
                            std::is_same_v<T, std::int64_t>)
                        {
                            bind.buffer_type =
                                MYSQL_TYPE_LONGLONG;

                            bind.buffer =
                                &value;

                            bind.buffer_length =
                                sizeof(value);

                            bind.is_unsigned =
                                false;
                        }
                        else if constexpr (
                            std::is_same_v<T, std::uint64_t>)
                        {
                            bind.buffer_type =
                                MYSQL_TYPE_LONGLONG;

                            bind.buffer =
                                &value;

                            bind.buffer_length =
                                sizeof(value);

                            bind.is_unsigned =
                                true;
                        }
                        else if constexpr (
                            std::is_same_v<T, double>)
                        {
                            bind.buffer_type =
                                MYSQL_TYPE_DOUBLE;

                            bind.buffer =
                                &value;

                            bind.buffer_length =
                                sizeof(value);
                        }
                        else if constexpr (
                            std::is_same_v<T, std::string>)
                        {
                            if (value.size() >
                                std::numeric_limits<
                                    unsigned long>::max())
                            {
                                throw std::length_error(
                                    "SQL parameter string is too large");
                            }

                            lengths_[i] =
                                static_cast<unsigned long>(
                                    value.size());

                            bind.buffer_type =
                                MYSQL_TYPE_STRING;

                            bind.buffer =
                                value.data();

                            bind.buffer_length =
                                static_cast<unsigned long>(
                                    value.size());

                            bind.length =
                                &lengths_[i];
                        }
                        else if constexpr (
                            std::is_same_v<
                                T,
                                std::nullptr_t>)
                        {
                            nullFlags_[i] = 1;

                            bind.buffer_type =
                                MYSQL_TYPE_NULL;

                            bind.buffer =
                                nullptr;

                            bind.buffer_length =
                                0;

                            bind.is_null =
                                &nullFlags_[i];
                        }
                    },
                    params_[i]);
            }
        }

    private:
        std::vector<jobpilot::core::SqlParam> params_;

        std::vector<MYSQL_BIND> binds_;

        std::vector<unsigned long> lengths_;

        std::unique_ptr<MysqlNullFlag[]> nullFlags_;
    };
}

namespace jobpilot::core
{
    Result ConnectionPool::Query(
        const std::string &sql,
        const std::vector<SqlParam> &params)
    {
        MYSQL *connection = Get();

        // RAII：
        // Query 正常返回或者抛出异常时，
        // 都自动把连接归还连接池。
        struct ConnectionGuard
        {
            ConnectionPool *pool;
            MYSQL *connection;

            ~ConnectionGuard()
            {
                if (pool != nullptr &&
                    connection != nullptr)
                {
                    pool->Return(connection);
                }
            }
        };

        ConnectionGuard connectionGuard{
            this,
            connection};

        MYSQL_STMT *rawStmt =
            mysql_stmt_init(connection);

        if (rawStmt == nullptr)
        {
            throw std::runtime_error(
                "mysql_stmt_init failed");
        }

        std::unique_ptr<
            MYSQL_STMT,
            decltype(&mysql_stmt_close)>
            stmt(
                rawStmt,
                &mysql_stmt_close);

        if (mysql_stmt_prepare(
                stmt.get(),
                sql.data(),
                static_cast<unsigned long>(
                    sql.size())) != 0)
        {
            throw std::runtime_error(
                std::string(
                    "mysql_stmt_prepare failed: ") +
                mysql_stmt_error(stmt.get()));
        }

        const unsigned long paramCount =
            mysql_stmt_param_count(
                stmt.get());

        if (paramCount != params.size())
        {
            throw std::invalid_argument(
                "SQL parameter count mismatch");
        }

        ParamBindContext paramContext(params);

        if (!params.empty())
        {
            if (mysql_stmt_bind_param(
                    stmt.get(),
                    paramContext.data()) != 0)
            {
                throw std::runtime_error(
                    std::string(
                        "mysql_stmt_bind_param failed: ") +
                    mysql_stmt_error(stmt.get()));
            }
        }

        if (mysql_stmt_execute(stmt.get()) != 0)
        {
            throw std::runtime_error(
                std::string(
                    "mysql_stmt_execute failed: ") +
                mysql_stmt_error(stmt.get()));
        }

        const unsigned int fieldCount =
            mysql_stmt_field_count(
                stmt.get());

        // 没有结果集，例如：
        // INSERT / UPDATE / DELETE。
        if (fieldCount == 0)
        {
            return Result{};
        }

        if (mysql_stmt_store_result(
                stmt.get()) != 0)
        {
            throw std::runtime_error(
                std::string(
                    "mysql_stmt_store_result failed: ") +
                mysql_stmt_error(stmt.get()));
        }

        MYSQL_RES *metadata =
            mysql_stmt_result_metadata(
                stmt.get());

        if (metadata == nullptr)
        {
            throw std::runtime_error(
                std::string(
                    "mysql_stmt_result_metadata failed: ") +
                mysql_stmt_error(stmt.get()));
        }

        std::unique_ptr<
            MYSQL_RES,
            decltype(&mysql_free_result)>
            metadataGuard(
                metadata,
                &mysql_free_result);

        MYSQL_FIELD *fields =
            mysql_fetch_fields(metadata);

        if (fields == nullptr)
        {
            throw std::runtime_error(
                "mysql_fetch_fields failed");
        }

        std::vector<std::string> columnNames;

        columnNames.reserve(fieldCount);

        for (unsigned int i = 0;
             i < fieldCount;
             ++i)
        {
            columnNames.emplace_back(
                fields[i].name != nullptr
                    ? fields[i].name
                    : "");
        }

        // 每列初始使用 64 KiB 缓冲区。
        // 如果字段超过该大小，
        // 则使用 mysql_stmt_fetch_column()
        // 重新获取完整字段。
        constexpr std::size_t kInitialBufferSize =
            64 * 1024;

        std::vector<std::vector<char>> buffers(
            fieldCount,
            std::vector<char>(
                kInitialBufferSize));

        std::vector<unsigned long> lengths(
            fieldCount,
            0);

        // 不能使用 vector<bool>。
        // 使用真正连续的 bool 数组，
        // 保证可以取得 bool* 传给 MYSQL_BIND。
        std::unique_ptr<MysqlNullFlag[]>
            nullFlags =
                fieldCount == 0
                    ? nullptr
                    : std::make_unique<
                          MysqlNullFlag[]>(
                          fieldCount);

        std::vector<MYSQL_BIND> resultBinds(
            fieldCount);

        for (unsigned int i = 0;
             i < fieldCount;
             ++i)
        {
            resultBinds[i] = MYSQL_BIND{};

            resultBinds[i].buffer_type =
                MYSQL_TYPE_STRING;

            resultBinds[i].buffer =
                buffers[i].data();

            resultBinds[i].buffer_length =
                static_cast<unsigned long>(
                    buffers[i].size());

            resultBinds[i].length =
                &lengths[i];

            resultBinds[i].is_null =
                &nullFlags[i];
        }

        if (mysql_stmt_bind_result(
                stmt.get(),
                resultBinds.data()) != 0)
        {
            throw std::runtime_error(
                std::string(
                    "mysql_stmt_bind_result failed: ") +
                mysql_stmt_error(stmt.get()));
        }

        std::vector<Result::Row> rows;

        while (true)
        {
            // 清理每一行的长度。
            std::fill(
                lengths.begin(),
                lengths.end(),
                0);

            // 清理每一行的 NULL 标志。
            for (unsigned int i = 0;
                 i < fieldCount;
                 ++i)
            {
                nullFlags[i] = 0;
            }

            const int fetchResult =
                mysql_stmt_fetch(
                    stmt.get());

            if (fetchResult == MYSQL_NO_DATA)
            {
                break;
            }

            if (fetchResult != 0 &&
                fetchResult != MYSQL_DATA_TRUNCATED)
            {
                throw std::runtime_error(
                    std::string(
                        "mysql_stmt_fetch failed: ") +
                    mysql_stmt_error(stmt.get()));
            }

            Result::Row row;

            row.reserve(fieldCount);

            for (unsigned int i = 0;
                 i < fieldCount;
                 ++i)
            {
                // SQL NULL -> std::nullopt
                if (nullFlags[i])
                {
                    row.emplace_back(
                        std::nullopt);

                    continue;
                }

                // 当前列数据超过初始缓冲区。
                if (fetchResult ==
                        MYSQL_DATA_TRUNCATED &&
                    lengths[i] >
                        buffers[i].size())
                {
                    if (static_cast<std::uintmax_t>(
                            lengths[i]) >=
                        static_cast<std::uintmax_t>(
                            std::numeric_limits<
                                std::size_t>::max()))
                    {
                        throw std::length_error(
                            "query result is too large");
                    }

                    const std::size_t fullSize =
                        static_cast<std::size_t>(
                            lengths[i]) +
                        1;

                    std::vector<char>
                        fullBuffer(
                            fullSize);

                    unsigned long fullLength =
                        0;

                    MysqlNullFlag fullNull =
                        0;

                    MYSQL_BIND columnBind{};

                    columnBind.buffer_type =
                        MYSQL_TYPE_STRING;

                    columnBind.buffer =
                        fullBuffer.data();

                    columnBind.buffer_length =
                        static_cast<unsigned long>(
                            fullBuffer.size());

                    columnBind.length =
                        &fullLength;

                    columnBind.is_null =
                        &fullNull;

                    if (mysql_stmt_fetch_column(
                            stmt.get(),
                            &columnBind,
                            i,
                            0) != 0)
                    {
                        throw std::runtime_error(
                            std::string(
                                "mysql_stmt_fetch_column failed: ") +
                            mysql_stmt_error(
                                stmt.get()));
                    }

                    if (fullNull)
                    {
                        row.emplace_back(
                            std::nullopt);
                    }
                    else
                    {
                        row.emplace_back(
                            std::string(
                                fullBuffer.data(),
                                fullLength));
                    }

                    continue;
                }

                row.emplace_back(
                    std::string(
                        buffers[i].data(),
                        lengths[i]));
            }

            rows.emplace_back(
                std::move(row));
        }

        return Result(
            std::move(columnNames),
            std::move(rows));
    }

    ConnectionPool::ConnectionPool(
        const std::string &host,
        std::uint16_t port,
        const std::string &user,
        const std::string &password,
        const std::string &database,
        std::size_t maxConn)
        : host_(host),
          port_(port),
          user_(user),
          password_(password),
          database_(database),
          idle_(),
          mutex_(),
          condition_(),
          total_(0),
          maxConn_(maxConn)
    {
        if (maxConn_ == 0)
        {
            throw std::invalid_argument(
                "maxConn must be greater than zero");
        }
    }

    ConnectionPool::~ConnectionPool()
    {
        std::lock_guard<std::mutex> lock(
            mutex_);

        while (!idle_.empty())
        {
            MYSQL *connection =
                idle_.front();

            idle_.pop_front();

            if (connection != nullptr)
            {
                ::mysql_close(connection);
            }
        }

        total_ = 0;
    }

    MYSQL *ConnectionPool::Get(int timeoutMs)
    {
        std::unique_lock<std::mutex> lock(
            mutex_);

        while (true)
        {
            // 1. 优先复用空闲连接。
            if (!idle_.empty())
            {
                MYSQL *connection =
                    idle_.front();

                idle_.pop_front();

                // 复用之前检查连接是否正常。
                if (connection != nullptr &&
                    ::mysql_ping(connection) == 0)
                {
                    return connection;
                }

                // 连接已经失效。
                if (connection != nullptr)
                {
                    ::mysql_close(connection);
                }

                --total_;

                continue;
            }

            // 2. 连接池尚未达到最大连接数，
            //    创建新连接。
            if (total_ < maxConn_)
            {
                MYSQL *connection =
                    ::mysql_init(nullptr);

                if (connection == nullptr)
                {
                    throw std::runtime_error(
                        "mysql_init failed");
                }

                MYSQL *connected =
                    ::mysql_real_connect(
                        connection,
                        host_.c_str(),
                        user_.c_str(),
                        password_.c_str(),
                        database_.c_str(),
                        port_,
                        nullptr,
                        0);

                if (connected == nullptr)
                {
                    const char *error =
                        ::mysql_error(
                            connection);

                    std::string message =
                        "mysql_real_connect failed";

                    if (error != nullptr)
                    {
                        message += ": ";
                        message += error;
                    }

                    ::mysql_close(connection);

                    throw std::runtime_error(
                        message);
                }

                ++total_;

                return connected;
            }

            // 3. 达到最大连接数，
            //    等待其他线程 Return()。
            const auto waitDuration =
                std::chrono::milliseconds(
                    timeoutMs);

            if (condition_.wait_for(
                    lock,
                    waitDuration) ==
                std::cv_status::timeout)
            {
                throw std::runtime_error(
                    "connection pool timeout");
            }
        }
    }

    void ConnectionPool::Return(
        MYSQL *connection)
    {
        if (connection == nullptr)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(
            mutex_);

        // 归还连接之前再次检查健康状态。
        if (::mysql_ping(connection) == 0)
        {
            idle_.push_back(connection);
        }
        else
        {
            ::mysql_close(connection);

            if (total_ > 0)
            {
                --total_;
            }
        }

        condition_.notify_one();
    }

} // namespace jobpilot::core
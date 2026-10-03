#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace jobpilot::core
{

    class Result
    {
    public:
        using Value = std::optional<std::string>;
        using Row = std::vector<Value>;

        Result() = default;

        Result(
            std::vector<std::string> columnNames,
            std::vector<Row> rows);

        std::size_t rowCount() const noexcept;

        std::size_t columnCount() const noexcept;

        const std::vector<std::string> &columnNames() const noexcept;

        const Row &row(std::size_t rowIndex) const;

        const Value &value(
            std::size_t rowIndex,
            std::size_t columnIndex) const;

    private:
        std::vector<std::string> columnNames_;
        std::vector<Row> rows_;
    };

} // namespace jobpilot::core
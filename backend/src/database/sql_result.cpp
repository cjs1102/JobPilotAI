#include "jobpilot/database/sql_result.h"

#include <stdexcept>
#include <utility>

namespace jobpilot::core
{

    Result::Result(
        std::vector<std::string> columnNames,
        std::vector<Row> rows)
        : columnNames_(std::move(columnNames)),
          rows_(std::move(rows))
    {
        for (const Row &row : rows_)
        {
            if (row.size() != columnNames_.size())
            {
                throw std::invalid_argument(
                    "result row column count mismatch");
            }
        }
    }

    std::size_t Result::rowCount() const noexcept
    {
        return rows_.size();
    }

    std::size_t Result::columnCount() const noexcept
    {
        return columnNames_.size();
    }

    const std::vector<std::string> &Result::columnNames() const noexcept
    {
        return columnNames_;
    }

    const Result::Row &Result::row(std::size_t rowIndex) const
    {
        return rows_.at(rowIndex);
    }

    const Result::Value &Result::value(
        std::size_t rowIndex,
        std::size_t columnIndex) const
    {
        return rows_.at(rowIndex).at(columnIndex);
    }

} // namespace jobpilot::core
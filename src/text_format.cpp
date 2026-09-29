// =============================================================================
// Copyright Martin Törnqvist <m.tornq@gmail.com>
//
// SPDX-License-Identifier: AGPL-3.0-or-later
// =============================================================================

#include "text_format.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <memory>

// -----------------------------------------------------------------------------
// Private
// -----------------------------------------------------------------------------

// Reads and removes the first word of the string.
static std::string read_and_remove_word(std::string& line)
{
    std::string str;

    for (auto it = std::begin(line); it != std::end(line);) {
        const char current_char = *it;

        line.erase(it);

        if (current_char == ' ') {
            break;
        }

        str += current_char;
    }

    return str;
}

static bool is_word_fit(
    const std::string& current_string,
    const std::string& word_to_fit,
    const size_t max_w)
{
    return (current_string.size() + word_to_fit.size() + 1) <= max_w;
}

// -----------------------------------------------------------------------------
// text_format
// -----------------------------------------------------------------------------
namespace text_format
{
std::vector<std::string> split(std::string line, const int max_w)
{
    if (line.empty()) {
        return {};
    }

    std::string current_word = read_and_remove_word(line);

    if (line.empty()) {
        return {current_word};
    }

    std::vector<std::string> result = {""};

    size_t current_row_idx = 0;

    while (!current_word.empty()) {
        if (!is_word_fit(result[current_row_idx], current_word, max_w)) {
            // Word did not fit on current line, make a new line
            ++current_row_idx;

            result.emplace_back("");
        }

        // If this is not the first word on the current line, add a
        // space before the word
        if (!result[current_row_idx].empty()) {
            result[current_row_idx] += " ";
        }

        result[current_row_idx] += current_word;

        current_word = read_and_remove_word(line);
    }

    return result;
}

std::vector<std::string> split_by_delim(
    std::string line,
    const char delim)
{
    if (line.empty()) {
        return {};
    }

    line.push_back(delim);

    std::vector<std::string> result;
    std::string current_line;

    for (const char c : line) {
        if (c == delim) {
            result.push_back(current_line);
            current_line = "";
        }
        else {
            current_line += c;
        }
    }

    return result;
}

std::vector<std::string> split_by_space(const std::string& line)
{
    return split_by_delim(line, ' ');
}

std::vector<std::string> split_by_newline(const std::string& line)
{
    return split_by_delim(line, '\n');
}

std::string replace_all(
    std::string line,
    const std::string& from,
    const std::string& to)
{
    if (from.empty()) {
        return line;
    }

    size_t start_pos = 0;

    while ((start_pos = line.find(from, start_pos)) != std::string::npos) {
        line.replace(start_pos, from.length(), to);

        start_pos += to.length();
    }

    return line;
}

std::string pad_before(
    const std::string& str,
    const size_t tot_w,
    const char c)
{
    std::string result = str;

    if (tot_w > str.size()) {
        result.insert(0, tot_w - str.size(), c);
    }

    return result;
}

std::string pad_after(
    const std::string& str,
    const size_t tot_w,
    const char c)
{
    std::string result = str;

    if (tot_w > str.size()) {
        result.insert(result.size(), tot_w - str.size(), c);
    }

    return result;
}

std::string first_to_lower(const std::string& str)
{
    std::string result = str;

    if (!result.empty()) {
        result[0] = (char)tolower(result[0]);
    }

    return result;
}

std::string first_to_upper(const std::string& str)
{
    std::string result = str;

    if (!result.empty()) {
        result[0] = (char)toupper(result[0]);
    }

    return result;
}

std::string to_upper(const std::string& str)
{
    std::string result = str;

    transform(
        std::begin(result),
        std::end(result),
        std::begin(result),
        ::toupper);

    return result;
}

void append_with_space(std::string& base_str, const std::string& addition)
{
    if (!base_str.empty() && !addition.empty()) {
        base_str += " ";
    }

    base_str += addition;
}

void append_as_comma_list(std::string& base_str, const std::string& addition)
{
    if (!base_str.empty() && !addition.empty()) {
        base_str += ", ";
    }

    base_str += addition;
}

std::string make_comma_and_str(const std::vector<std::string>& strings)
{
    if (strings.empty()) {
        return "";
    }

    const size_t nr_strings = strings.size();
    const size_t last_idx = nr_strings - 1;

    std::string result = strings[0];

    // Loop from index 1 to the end and append strings with "," or "and".
    for (size_t i = 1; i < nr_strings; ++i) {
        if (i < last_idx) {
            result += ", " + strings[i];
        }
        else {
            // Hm, Oxford comma or not? ;-)
            result += " and " + strings[i];
        }
    }

    return result;
}

std::string trim_leading_and_trailing_spaces(const std::string& str)
{
    auto start = std::find_if_not(std::begin(str), std::end(str), ::isspace);
    auto end = std::find_if_not(std::rbegin(str), std::rend(str), ::isspace).base();

    if (start >= end) {
        // The string contains only spaces - return an empty string.
        return "";
    }

    return {start, end};
}

}  // namespace text_format

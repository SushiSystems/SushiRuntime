/**************************************************************************/
/* placement_journal.cpp                                                  */
/**************************************************************************/
/*                          This file is part of:                         */
/*                              SushiRuntime                              */
/*              https://github.com/SushiSystems/SushiRuntime              */
/*                         https://sushisystems.io                        */
/**************************************************************************/
/* Copyright (c) 2026-present Mustafa Garip & Sushi Systems               */
/*                                                                        */
/* Licensed under the Apache License, Version 2.0 (the "License"); you    */
/* may not use this file except in compliance with the License. You may   */
/* obtain a copy of the License at                                        */
/*                                                                        */
/*     https://www.apache.org/licenses/LICENSE-2.0                        */
/*                                                                        */
/* Unless required by applicable law or agreed to in writing, software    */
/* distributed under the License is distributed on an "AS IS" BASIS,      */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or        */
/* implied. See the License for the specific language governing           */
/* permissions and limitations under the License.                         */
/**************************************************************************/

/**
 * @file placement_journal.cpp
 * @brief Implements PlacementJournal and its text format.
 * @see modules/engine/partition_session/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/partition_session/placement_journal.hpp>

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <ios>
#include <locale>
#include <sstream>
#include <string>
#include <utility>

#include <SushiRuntime/core/error.hpp>
#include <SushiRuntime/device_profile/device_profile_file.hpp>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            /** @brief Holds the keywords of the format. */
            constexpr const char* K_VERSION_KEY = "placement_journal_version";
            constexpr const char* K_ENTRY_KEY = "entry";
            constexpr const char* K_HORIZON_KEY = "horizon";
            constexpr const char* K_NEXT_HORIZON = "next";
            constexpr const char* K_PROBLEM_KEY = "problem_hash";
            constexpr const char* K_ASSIGNMENT_KEY = "assignment_hash";
            constexpr const char* K_MODELLED_KEY = "modelled_step";
            constexpr const char* K_MEASURED_KEY = "measured_step";
            constexpr const char* K_FLOOR_KEY = "floor_step";
            constexpr const char* K_DEVICE_KEY = "measured_device";
            constexpr const char* K_PROFILES_KEY = "profiles";
            constexpr const char* K_END_KEY = "end_entry";

            /** @brief Holds the words of each PlacementReason, in enumerator order. */
            constexpr const char* K_REASONS[] = {"first", "replan", "try_floor", "keep_floor", "leave_floor"};

            /** @brief Holds the words of each PlannedBy, in enumerator order. */
            constexpr const char* K_PLANNERS[] = {"scheduler", "floor"};

            /** @brief Returns the error of a malformed line. */
            [[nodiscard]] Error fault(std::size_t line, const std::string& what)
            {
                return Error{ErrorCode::InvalidArgument, "placement journal, line " + std::to_string(line) + ": " + what};
            }

            /** @brief Returns the words of line, split at spaces. */
            [[nodiscard]] std::vector<std::string> words_of(const std::string& line)
            {
                std::istringstream stream(line);
                std::vector<std::string> words;
                std::string word;
                while (stream >> word)
                    words.push_back(word);
                return words;
            }

            /** @brief Returns the unsigned number text holds in base, or nothing. */
            [[nodiscard]] std::optional<std::uint64_t> to_unsigned(const std::string& text, int base)
            {
                if (text.empty() || text[0] == '-')
                    return std::nullopt;
                errno = 0;
                char* end = nullptr;
                const unsigned long long value = std::strtoull(text.c_str(), &end, base);
                if (errno != 0 || end != text.c_str() + text.size())
                    return std::nullopt;
                return static_cast<std::uint64_t>(value);
            }

            /** @brief Returns the number text holds in the classic locale, or nothing. */
            [[nodiscard]] std::optional<double> to_double(const std::string& text)
            {
                std::istringstream stream(text);
                stream.imbue(std::locale::classic());
                double value = 0.0;
                stream >> value;
                if (stream.fail() || stream.peek() != std::char_traits<char>::eof())
                    return std::nullopt;
                return value;
            }

            /** @brief Returns the index of word in table, or nothing. */
            template <std::size_t Count>
            [[nodiscard]] std::optional<std::size_t> index_of(const char* const (&table)[Count], const std::string& word)
            {
                for (std::size_t index = 0; index < Count; ++index)
                {
                    if (word == table[index])
                        return index;
                }
                return std::nullopt;
            }

            /** @brief Returns the text of one entry, profiles included. */
            [[nodiscard]] Result<std::string> entry_text(const PlacementJournalEntry& entry)
            {
                Result<std::string> profiles = DeviceProfileFile::serialise(entry.profiles);
                if (!profiles)
                    return make_error(std::move(profiles.error()));
                std::size_t profile_lines = 0;
                for (const char letter : profiles.value())
                    profile_lines += letter == '\n' ? 1 : 0;

                std::ostringstream text;
                text.imbue(std::locale::classic());
                text << std::setprecision(17);
                text << K_ENTRY_KEY << ' ' << entry.sequence << ' ' << K_REASONS[static_cast<std::size_t>(entry.reason)]
                     << ' ' << K_PLANNERS[static_cast<std::size_t>(entry.planned_by)] << '\n';
                text << K_HORIZON_KEY << ' ';
                if (entry.horizon)
                    text << *entry.horizon << '\n';
                else
                    text << K_NEXT_HORIZON << '\n';
                text << K_PROBLEM_KEY << ' ' << std::hex << std::setw(16) << std::setfill('0') << entry.problem_hash << '\n';
                text << K_ASSIGNMENT_KEY << ' ' << std::setw(16) << entry.assignment_hash << std::dec << std::setfill(' ')
                     << '\n';
                text << K_MODELLED_KEY << ' ' << entry.modelled_step_seconds << '\n';
                text << K_MEASURED_KEY << ' ' << entry.measured_step_seconds << '\n';
                text << K_FLOOR_KEY << ' ' << entry.floor_step_seconds << '\n';
                for (const auto& device : entry.measured_device_seconds)
                    text << K_DEVICE_KEY << ' ' << to_text(device.first) << ' ' << device.second << '\n';
                text << K_PROFILES_KEY << ' ' << profile_lines << '\n' << profiles.value() << K_END_KEY << '\n';
                return text.str();
            }

            /** @brief Returns the version line. */
            [[nodiscard]] std::string version_line()
            {
                return std::string(K_VERSION_KEY) + ' ' + std::to_string(PlacementJournal::K_CURRENT_VERSION) + '\n';
            }

            /** @brief Reads the entries of a text, one line at a time. */
            class JournalReader final
            {
                public:
                    /** @brief Builds a reader over text. */
                    explicit JournalReader(std::string_view text) : lines_(std::string(text))
                    {

                    }

                    /** @brief Returns every entry of the text, or the error of its first malformed line. */
                    [[nodiscard]] Result<std::vector<PlacementJournalEntry>> read()
                    {
                        std::vector<PlacementJournalEntry> entries;
                        std::string line;
                        if (!next(line))
                            return make_error(fault(number_, "the text is empty"));
                        Result<void> versioned = read_version(line);
                        if (!versioned)
                            return make_error(std::move(versioned.error()));
                        while (next(line))
                        {
                            Result<PlacementJournalEntry> entry = read_entry(line);
                            if (!entry)
                                return make_error(std::move(entry.error()));
                            entries.push_back(std::move(entry.value()));
                        }
                        return entries;
                    }

                private:
                    /**
                     * @brief Reads the version line into version_.
                     * @return InvalidArgument for a malformed line, Unsupported for a version this reader does
                     *         not know, naming it and the versions it reads.
                     */
                    [[nodiscard]] Result<void> read_version(const std::string& line)
                    {
                        const std::vector<std::string> words = words_of(line);
                        if (words.size() != 2 || words[0] != K_VERSION_KEY)
                            return make_error(fault(number_, "the first line must be '" + std::string(K_VERSION_KEY) +
                                                                 " <number>'"));
                        const std::optional<std::uint64_t> version = to_unsigned(words[1], 10);
                        if (!version)
                            return make_error(fault(number_, "the version is not a number"));
                        if (*version < static_cast<std::uint64_t>(PlacementJournal::K_OLDEST_VERSION) ||
                            *version > static_cast<std::uint64_t>(PlacementJournal::K_CURRENT_VERSION))
                            return make_error(Error{ErrorCode::Unsupported,
                                                    "placement journal has version " + words[1] +
                                                        ", this reader knows versions " +
                                                        std::to_string(PlacementJournal::K_OLDEST_VERSION) + " to " +
                                                        std::to_string(PlacementJournal::K_CURRENT_VERSION)});
                        version_ = *version;
                        return {};
                    }

                    /**
                     * @brief Reads the horizon line of a version 2 entry into entry.
                     *
                     * A version 1 entry has no such line; it keeps no horizon, which is its upgrade to
                     * version 2: it applies at the next horizon.
                     */
                    [[nodiscard]] Result<void> read_horizon(PlacementJournalEntry& entry)
                    {
                        if (version_ < 2)
                            return {};
                        Result<std::vector<std::string>> words = keyed(K_HORIZON_KEY, 1);
                        if (!words)
                            return make_error(std::move(words.error()));
                        const std::string& value = words.value()[1];
                        if (value == K_NEXT_HORIZON)
                            return {};
                        const std::optional<std::uint64_t> horizon = to_unsigned(value, 10);
                        if (!horizon)
                            return make_error(fault(number_, "'horizon' holds a bad number"));
                        entry.horizon = *horizon;
                        return {};
                    }

                    /** @brief Reads the next line into line; false at the end of the text. */
                    bool next(std::string& line)
                    {
                        if (!std::getline(lines_, line))
                            return false;
                        ++number_;
                        return true;
                    }

                    /** @brief Reads the next line, which must open with key and hold count words after it. */
                    [[nodiscard]] Result<std::vector<std::string>> keyed(const char* key, std::size_t count)
                    {
                        std::string line;
                        if (!next(line))
                            return make_error(fault(number_, std::string("the text ends before '") + key + "'"));
                        std::vector<std::string> words = words_of(line);
                        if (words.size() != count + 1 || words[0] != key)
                            return make_error(fault(number_, std::string("expected '") + key + "' and " +
                                                                 std::to_string(count) + " values"));
                        return words;
                    }

                    /** @brief Reads the number line of key into value. */
                    [[nodiscard]] Result<void> read_double(const char* key, double& value)
                    {
                        Result<std::vector<std::string>> words = keyed(key, 1);
                        if (!words)
                            return make_error(std::move(words.error()));
                        const std::optional<double> parsed = to_double(words.value()[1]);
                        if (!parsed)
                            return make_error(fault(number_, std::string("'") + key + "' holds a bad number"));
                        value = *parsed;
                        return {};
                    }

                    /** @brief Reads the hash line of key into value. */
                    [[nodiscard]] Result<void> read_hash(const char* key, std::uint64_t& value)
                    {
                        Result<std::vector<std::string>> words = keyed(key, 1);
                        if (!words)
                            return make_error(std::move(words.error()));
                        const std::optional<std::uint64_t> parsed = to_unsigned(words.value()[1], 16);
                        if (!parsed || words.value()[1].size() != 16)
                            return make_error(fault(number_, std::string("'") + key + "' holds a bad hash"));
                        value = *parsed;
                        return {};
                    }

                    /** @brief Reads one entry whose first line is head. */
                    [[nodiscard]] Result<PlacementJournalEntry> read_entry(const std::string& head)
                    {
                        PlacementJournalEntry entry;
                        const std::vector<std::string> words = words_of(head);
                        if (words.size() != 4 || words[0] != K_ENTRY_KEY)
                            return make_error(fault(number_, "expected 'entry <sequence> <reason> <planner>'"));
                        const std::optional<std::uint64_t> sequence = to_unsigned(words[1], 10);
                        const std::optional<std::size_t> reason = index_of(K_REASONS, words[2]);
                        const std::optional<std::size_t> planner = index_of(K_PLANNERS, words[3]);
                        if (!sequence || !reason || !planner)
                            return make_error(fault(number_, "the entry line holds a bad sequence, reason or planner"));
                        entry.sequence = *sequence;
                        entry.reason = static_cast<PlacementReason>(*reason);
                        entry.planned_by = static_cast<PlannedBy>(*planner);
                        Result<void> horizon = read_horizon(entry);
                        if (!horizon)
                            return make_error(std::move(horizon.error()));

                        for (const Result<void>& read :
                             {read_hash(K_PROBLEM_KEY, entry.problem_hash), read_hash(K_ASSIGNMENT_KEY, entry.assignment_hash),
                              read_double(K_MODELLED_KEY, entry.modelled_step_seconds),
                              read_double(K_MEASURED_KEY, entry.measured_step_seconds),
                              read_double(K_FLOOR_KEY, entry.floor_step_seconds)})
                        {
                            if (!read)
                                return make_error(read.error());
                        }

                        std::string line;
                        while (next(line))
                        {
                            const std::vector<std::string> fields = words_of(line);
                            if (fields.size() == 2 && fields[0] == K_PROFILES_KEY)
                                return read_profiles(fields[1], entry);
                            if (fields.size() != 3 || fields[0] != K_DEVICE_KEY)
                                return make_error(fault(number_, "expected 'measured_device' or 'profiles'"));
                            const std::optional<DeviceKey> key = parse_device_key(fields[1]);
                            const std::optional<double> seconds = to_double(fields[2]);
                            if (!key || !seconds || entry.measured_device_seconds.count(*key) != 0)
                                return make_error(fault(number_, "'measured_device' holds a bad or repeated device"));
                            entry.measured_device_seconds.emplace(*key, *seconds);
                        }
                        return make_error(fault(number_, "the entry has no 'profiles' line"));
                    }

                    /** @brief Reads the profile lines counted by count and the closing line into entry. */
                    [[nodiscard]] Result<PlacementJournalEntry> read_profiles(const std::string& count,
                                                                             PlacementJournalEntry& entry)
                    {
                        const std::optional<std::uint64_t> lines = to_unsigned(count, 10);
                        if (!lines)
                            return make_error(fault(number_, "'profiles' holds a bad line count"));
                        std::string text;
                        std::string line;
                        for (std::uint64_t index = 0; index < *lines; ++index)
                        {
                            if (!next(line))
                                return make_error(fault(number_, "the text ends inside the profiles"));
                            text += line;
                            text += '\n';
                        }
                        Result<DeviceProfileSet> profiles = DeviceProfileFile::parse(text);
                        if (!profiles)
                            return make_error(fault(number_, "the profiles do not parse: " + profiles.error().message));
                        entry.profiles = std::move(profiles.value());
                        if (!next(line) || line != K_END_KEY)
                            return make_error(fault(number_, "expected 'end_entry'"));
                        return std::move(entry);
                    }

                    /// @brief The lines of the text.
                    std::istringstream lines_;

                    /// @brief Number of the last line read, from one.
                    std::size_t number_ = 0;

                    /// @brief The version the text declares.
                    std::uint64_t version_ = 0;
            };
        } // namespace

        PlacementJournal::PlacementJournal(std::optional<std::filesystem::path> file) : file_(std::move(file))
        {

        }

        Result<void> PlacementJournal::append(PlacementJournalEntry entry)
        {
            entry.sequence = entries_.size();
            entries_.push_back(std::move(entry));
            if (!file_)
                return {};
            Result<std::string> text = entry_text(entries_.back());
            if (!text)
                return make_error(std::move(text.error()));
            // The first entry starts the file, so the file holds exactly serialise(entries_).
            const bool first = entries_.size() == 1;
            std::ofstream out(*file_, std::ios::binary | (first ? std::ios::trunc : std::ios::app));
            if (first)
                out << version_line();
            out << text.value();
            out.flush();
            if (!out)
                return make_error(Error{ErrorCode::InvalidArgument,
                                        "the placement journal " + file_->string() + " could not be written"});
            return {};
        }

        Result<std::string> PlacementJournal::serialise(const std::vector<PlacementJournalEntry>& entries)
        {
            std::string text = version_line();
            for (const PlacementJournalEntry& entry : entries)
            {
                Result<std::string> written = entry_text(entry);
                if (!written)
                    return make_error(std::move(written.error()));
                text += written.value();
            }
            return text;
        }

        Result<std::vector<PlacementJournalEntry>> PlacementJournal::parse(std::string_view text)
        {
            return JournalReader(text).read();
        }

        Result<std::vector<PlacementJournalEntry>> PlacementJournal::read(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
                return make_error(Error{ErrorCode::InvalidArgument,
                                        "the placement journal " + path.string() + " could not be opened"});
            std::ostringstream text;
            text << file.rdbuf();
            return parse(text.str());
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime

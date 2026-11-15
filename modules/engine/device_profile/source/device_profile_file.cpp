/**************************************************************************/
/* device_profile_file.cpp                                              */
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
 * @file device_profile_file.cpp
 * @brief Implements DeviceProfileFile.
 * @see modules/engine/device_profile/README.md
 * @author Mustafa Garip
 */

#include <SushiRuntime/device_profile/device_profile_file.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <locale>
#include <optional>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

namespace SushiRuntime
{
    namespace Internal::Coherence
    {
        namespace
        {
            /** @brief Holds the keywords of the file. */
            constexpr const char* K_VERSION_KEY = "format_version";
            constexpr const char* K_DEVICE_KEY = "device";
            constexpr const char* K_CAPACITY_KEY = "capacity_bytes";
            constexpr const char* K_RESERVE_KEY = "reserve_bytes";
            constexpr const char* K_MEMORY_BANDWIDTH_KEY = "memory_bandwidth";
            constexpr const char* K_BASE_THROUGHPUT_KEY = "base_throughput";
            constexpr const char* K_THROUGHPUT_KEY = "throughput";
            constexpr const char* K_PATH_KEY = "path";
            constexpr const char* K_END_KEY = "end";
            constexpr const char* K_HOST_TO_DEVICE_KEY = "host_to_device";
            constexpr const char* K_DEVICE_TO_HOST_KEY = "device_to_host";

            /**
             * @brief Upgrades a version 1 set to version 2.
             *
             * Version 1 has no host links, so every device keeps its links unmeasured.
             */
            [[nodiscard]] DeviceProfileSet upgrade_from_version_1(DeviceProfileSet profiles)
            {
                return profiles;
            }

            /** @brief Returns an Error result for line number line. */
            template <typename T>
            [[nodiscard]] Result<T> line_error(ErrorCode code, std::size_t line, const std::string& what)
            {
                return make_error(
                    Error{code, "device profile file, line " + std::to_string(line) + ": " + what});
            }

            /** @brief Returns a stream that formats numbers the same way on every machine. */
            [[nodiscard]] std::ostringstream make_stream()
            {
                std::ostringstream stream;
                stream.imbue(std::locale::classic());
                stream << std::setprecision(17);
                return stream;
            }

            /** @brief Returns the words of one line. */
            [[nodiscard]] std::vector<std::string> split_words(const std::string& line)
            {
                std::istringstream stream(line);
                stream.imbue(std::locale::classic());
                return std::vector<std::string>(std::istream_iterator<std::string>(stream),
                                                std::istream_iterator<std::string>());
            }

            /** @brief Returns the unsigned integer that word spells, or nothing. */
            [[nodiscard]] std::optional<std::uint64_t> parse_unsigned(const std::string& word)
            {
                if (word.empty() || word.size() > 20)
                    return std::nullopt;
                std::uint64_t value = 0;
                for (const char digit : word)
                {
                    if (digit < '0' || digit > '9')
                        return std::nullopt;
                    const std::uint64_t digit_value = static_cast<std::uint64_t>(digit - '0');
                    if (value > (UINT64_MAX - digit_value) / 10)
                        return std::nullopt;
                    value = value * 10 + digit_value;
                }
                return value;
            }

            /** @brief Returns the finite, non-negative number that word spells, or nothing. */
            [[nodiscard]] std::optional<double> parse_measure(const std::string& word)
            {
                std::istringstream stream(word);
                stream.imbue(std::locale::classic());
                double value = 0.0;
                stream >> value;
                if (stream.fail() || stream.peek() != std::char_traits<char>::eof())
                    return std::nullopt;
                if (!std::isfinite(value) || value < 0.0)
                    return std::nullopt;
                return value;
            }

            /** @brief Tells which scalar keys of the open device block were seen. */
            struct SeenKeys
            {
                bool capacity = false;
                bool reserve = false;
                bool memory_bandwidth = false;
                bool base_throughput = false;
            };

            /** @brief Reads the file one line at a time into a DeviceProfileSet. */
            class Parser
            {
                public:
                    /** @brief Reads text; the result is the set or the first fault. */
                    [[nodiscard]] Result<DeviceProfileSet> run(std::string_view text)
                    {
                        std::istringstream stream{std::string(text)};
                        std::string line;
                        std::size_t number = 0;
                        while (std::getline(stream, line))
                        {
                            ++number;
                            if (!line.empty() && line.back() == '\r')
                                line.pop_back();
                            const std::vector<std::string> words = split_words(line);
                            if (words.empty())
                                continue;
                            const std::optional<Error> fault = take_line(number, words);
                            if (fault)
                                return make_error(Error{*fault});
                        }
                        if (!version_seen_)
                            return line_error<DeviceProfileSet>(ErrorCode::InvalidArgument, number + 1,
                                                                "no format_version line");
                        if (open_)
                            return line_error<DeviceProfileSet>(ErrorCode::InvalidArgument, number + 1,
                                                                "device block without end");
                        if (version_ == 1)
                            return upgrade_from_version_1(std::move(profiles_));
                        return std::move(profiles_);
                    }

                private:
                    /** @brief Applies one line; returns the fault, if any. */
                    [[nodiscard]] std::optional<Error> take_line(std::size_t number,
                                                                 const std::vector<std::string>& words)
                    {
                        const std::string& key = words[0];
                        if (!version_seen_)
                            return take_version(number, words);
                        if (key == K_DEVICE_KEY)
                            return take_device(number, words);
                        if (key == K_END_KEY)
                            return take_end(number, words);
                        if (!open_)
                            return fault(number, "'" + key + "' outside a device block");
                        if (key == K_THROUGHPUT_KEY)
                            return take_throughput(number, words);
                        if (key == K_PATH_KEY)
                            return take_path(number, words);
                        if (version_ >= 2 && (key == K_HOST_TO_DEVICE_KEY || key == K_DEVICE_TO_HOST_KEY))
                            return take_host_link(number, words);
                        return take_scalar(number, words);
                    }

                    /** @brief Reads the version line. */
                    [[nodiscard]] std::optional<Error> take_version(std::size_t number,
                                                                    const std::vector<std::string>& words)
                    {
                        if (words[0] != K_VERSION_KEY || words.size() != 2)
                            return fault(number, "the first line must be 'format_version <number>'");
                        const std::optional<std::uint64_t> version = parse_unsigned(words[1]);
                        if (!version)
                            return fault(number, "the version is not a number");
                        if (*version < static_cast<std::uint64_t>(DeviceProfileFile::K_OLDEST_VERSION) ||
                            *version > static_cast<std::uint64_t>(DeviceProfileFile::K_CURRENT_VERSION))
                            return Error{ErrorCode::Unsupported,
                                         "device profile file has format_version " + words[1] +
                                             ", this reader knows versions " +
                                             std::to_string(DeviceProfileFile::K_OLDEST_VERSION) + " to " +
                                             std::to_string(DeviceProfileFile::K_CURRENT_VERSION)};
                        version_ = *version;
                        version_seen_ = true;
                        return std::nullopt;
                    }

                    /** @brief Opens a device block. */
                    [[nodiscard]] std::optional<Error> take_device(std::size_t number,
                                                                   const std::vector<std::string>& words)
                    {
                        if (open_)
                            return fault(number, "'device' inside a device block");
                        if (words.size() != 2)
                            return fault(number, "'device' takes one device key");
                        const std::optional<DeviceKey> key = parse_device_key(words[1]);
                        if (!key)
                            return fault(number, "'" + words[1] + "' is not a device key");
                        if (profiles_.has_profile(*key))
                            return fault(number, "device " + words[1] + " appears twice");
                        key_ = *key;
                        profile_ = DeviceProfile{};
                        seen_ = SeenKeys{};
                        open_ = true;
                        return std::nullopt;
                    }

                    /** @brief Closes the device block and stores its profile. */
                    [[nodiscard]] std::optional<Error> take_end(std::size_t number,
                                                                const std::vector<std::string>& words)
                    {
                        if (!open_ || words.size() != 1)
                            return fault(number, "'end' closes an open device block");
                        if (!seen_.capacity || !seen_.reserve || !seen_.memory_bandwidth ||
                            !seen_.base_throughput)
                            return fault(number, "a device block lacks a scalar key");
                        profiles_.set(key_, std::move(profile_));
                        open_ = false;
                        return std::nullopt;
                    }

                    /** @brief Reads a throughput line. */
                    [[nodiscard]] std::optional<Error> take_throughput(std::size_t number,
                                                                       const std::vector<std::string>& words)
                    {
                        if (words.size() != 3)
                            return fault(number, "'throughput' takes a kind and a value");
                        const std::optional<std::uint64_t> kind = parse_unsigned(words[1]);
                        const std::optional<double> value = parse_measure(words[2]);
                        if (!kind || *kind > UINT32_MAX || !value)
                            return fault(number, "'throughput' holds a bad kind or value");
                        const bool inserted = profile_.throughputs
                                                  .emplace(OperationKind(static_cast<std::uint32_t>(*kind)), *value)
                                                  .second;
                        if (!inserted)
                            return fault(number, "kind " + words[1] + " appears twice");
                        return std::nullopt;
                    }

                    /** @brief Reads a path line. */
                    [[nodiscard]] std::optional<Error> take_path(std::size_t number,
                                                                 const std::vector<std::string>& words)
                    {
                        if (words.size() != 4)
                            return fault(number, "'path' takes a name, a bandwidth and a latency");
                        const std::optional<double> bandwidth = parse_measure(words[2]);
                        const std::optional<double> latency = parse_measure(words[3]);
                        if (!bandwidth || !latency)
                            return fault(number, "'path' holds a bad bandwidth or latency");
                        if (profile_.find_path(TransferPathID::named(words[1])) != nullptr)
                            return fault(number, "path " + words[1] + " appears twice");
                        profile_.set_path(words[1], PathProfile{*bandwidth, *latency, {}});
                        return std::nullopt;
                    }

                    /** @brief Reads a host_to_device or a device_to_host line. */
                    [[nodiscard]] std::optional<Error> take_host_link(std::size_t number,
                                                                      const std::vector<std::string>& words)
                    {
                        const std::string& key = words[0];
                        if (words.size() != 3)
                            return fault(number, "'" + key + "' takes a bandwidth and a latency");
                        const std::optional<double> bandwidth = parse_measure(words[1]);
                        const std::optional<double> latency = parse_measure(words[2]);
                        if (!bandwidth || !latency)
                            return fault(number, "'" + key + "' holds a bad bandwidth or latency");
                        std::optional<PathProfile>& link =
                            key == K_HOST_TO_DEVICE_KEY ? profile_.host_to_device : profile_.device_to_host;
                        if (link)
                            return fault(number, "'" + key + "' appears twice");
                        link = PathProfile{*bandwidth, *latency};
                        return std::nullopt;
                    }

                    /** @brief Reads one of the four scalar lines. */
                    [[nodiscard]] std::optional<Error> take_scalar(std::size_t number,
                                                                   const std::vector<std::string>& words)
                    {
                        const std::string& key = words[0];
                        if (words.size() != 2)
                            return fault(number, "'" + key + "' takes one value");
                        if (key == K_CAPACITY_KEY || key == K_RESERVE_KEY)
                        {
                            const std::optional<std::uint64_t> value = parse_unsigned(words[1]);
                            bool& seen = key == K_CAPACITY_KEY ? seen_.capacity : seen_.reserve;
                            if (!value || seen)
                                return fault(number, "'" + key + "' is repeated or not a number");
                            (key == K_CAPACITY_KEY ? profile_.capacity_bytes : profile_.reserve_bytes) = *value;
                            seen = true;
                            return std::nullopt;
                        }
                        if (key == K_MEMORY_BANDWIDTH_KEY || key == K_BASE_THROUGHPUT_KEY)
                        {
                            const std::optional<double> value = parse_measure(words[1]);
                            bool& seen =
                                key == K_MEMORY_BANDWIDTH_KEY ? seen_.memory_bandwidth : seen_.base_throughput;
                            if (!value || seen)
                                return fault(number, "'" + key + "' is repeated or not a number");
                            (key == K_MEMORY_BANDWIDTH_KEY ? profile_.memory_bandwidth
                                                           : profile_.base_throughput) = *value;
                            seen = true;
                            return std::nullopt;
                        }
                        return fault(number, "unknown key '" + key + "'");
                    }

                    /** @brief Returns the fault of line number. */
                    [[nodiscard]] static Error fault(std::size_t number, const std::string& what)
                    {
                        return Error{ErrorCode::InvalidArgument,
                                     "device profile file, line " + std::to_string(number) + ": " + what};
                    }

                    DeviceProfileSet profiles_; /**< The set read so far. */
                    DeviceProfile profile_;     /**< The block being read. */
                    DeviceKey key_;             /**< Key of the block being read. */
                    SeenKeys seen_;             /**< Scalar keys of the block seen so far. */
                    std::uint64_t version_ = 0; /**< Version the file declares. */
                    bool version_seen_ = false; /**< Whether the version line has been read. */
                    bool open_ = false;         /**< Whether a device block is open. */
            };

            /** @brief Returns whether name can be written as one word. */
            [[nodiscard]] bool is_one_word(const std::string& name)
            {
                if (name.empty())
                    return false;
                for (const char letter : name)
                {
                    if (letter == ' ' || letter == '\t' || letter == '\n' || letter == '\r')
                        return false;
                }
                return true;
            }
        } // namespace

        Result<std::string> DeviceProfileFile::serialise(const DeviceProfileSet& profiles)
        {
            std::ostringstream stream = make_stream();
            stream << K_VERSION_KEY << ' ' << K_CURRENT_VERSION << '\n';
            for (const auto& [key, profile] : profiles.profiles())
            {
                stream << '\n' << K_DEVICE_KEY << ' ' << to_text(key) << '\n';
                stream << K_CAPACITY_KEY << ' ' << profile.capacity_bytes << '\n';
                stream << K_RESERVE_KEY << ' ' << profile.reserve_bytes << '\n';
                stream << K_MEMORY_BANDWIDTH_KEY << ' ' << profile.memory_bandwidth << '\n';
                stream << K_BASE_THROUGHPUT_KEY << ' ' << profile.base_throughput << '\n';
                for (const auto& [kind, throughput] : profile.throughputs)
                    stream << K_THROUGHPUT_KEY << ' ' << kind.value << ' ' << throughput << '\n';
                std::vector<const PathProfile*> named_paths;
                for (const auto& entry : profile.paths)
                    named_paths.push_back(&entry.second);
                std::sort(named_paths.begin(), named_paths.end(),
                          [](const PathProfile* left, const PathProfile* right) { return left->name < right->name; });
                for (const PathProfile* path : named_paths)
                {
                    if (!is_one_word(path->name))
                        return make_error(Error{ErrorCode::InvalidArgument,
                                                "a transfer path name must be one word: '" + path->name + "'"});
                    stream << K_PATH_KEY << ' ' << path->name << ' ' << path->bandwidth << ' ' << path->latency
                           << '\n';
                }
                if (profile.host_to_device)
                    stream << K_HOST_TO_DEVICE_KEY << ' ' << profile.host_to_device->bandwidth << ' '
                           << profile.host_to_device->latency << '\n';
                if (profile.device_to_host)
                    stream << K_DEVICE_TO_HOST_KEY << ' ' << profile.device_to_host->bandwidth << ' '
                           << profile.device_to_host->latency << '\n';
                stream << K_END_KEY << '\n';
            }
            return stream.str();
        }

        Result<DeviceProfileSet> DeviceProfileFile::parse(std::string_view text)
        {
            return Parser().run(text);
        }

        Result<DeviceProfileSet> DeviceProfileFile::read(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file)
                return make_error(Error{ErrorCode::InvalidArgument,
                                        "cannot open the device profile file " + path.string()});
            const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            return parse(text);
        }

        Result<bool> DeviceProfileFile::write(const std::filesystem::path& path,
                                              const DeviceProfileSet& profiles)
        {
            Result<std::string> text = serialise(profiles);
            if (!text)
                return make_error(Error{text.error()});

            std::filesystem::path temporary = path;
            temporary += ".tmp";
            {
                std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
                if (!file)
                    return make_error(Error{ErrorCode::InvalidState,
                                            "cannot write the device profile file " + temporary.string()});
                file << text.value();
                file.flush();
                if (!file)
                    return make_error(Error{ErrorCode::InvalidState,
                                            "cannot write the device profile file " + temporary.string()});
            }
            std::error_code failure;
            std::filesystem::rename(temporary, path, failure);
            if (failure)
            {
                std::filesystem::remove(temporary, failure);
                return make_error(Error{ErrorCode::InvalidState,
                                        "cannot replace the device profile file " + path.string()});
            }
            return true;
        }
    } // namespace Internal::Coherence
} // namespace SushiRuntime

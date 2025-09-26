/*
* Copyright (C) 2017  Jaroslav Safka
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef LOG_MOCK_H
#define LOG_MOCK_H

#include <sstream>
#include <iomanip>
#include <map>
#include <string>
#include <list>
#include <any>
#include <cstdint>

/**
 * @brief Class for test logging in mocked files / classes
 *
 * usage:
 In mocked file:
    // fce must be used to avoid Static Initialization Order Fiasco
    LogMock& getCMessageQueueSocketLog()
    {
        static LogMock obj;
        return obj;
    }

In mocked member functions:
    // logging of data for later test check
    getCMessageQueueSocketLog().log() << "call sendRequest(" << nodeId << ", " << msgId << ", ";
    getCMessageQueueSocketLog().logBuffer(pPayload, nPayloadWords);
    getCMessageQueueSocketLog().saveLog();  // will add log entry and clear the stream

    // pop testing data and use
    std::vector<uint32_t> payload{};
    getCMessageQueueSocketLog().popData("MailBoxData32", &payload);

In test file:
    extern LogMock& getCMessageQueueSocketLog();

in testcase fce:
    getCMessageQueueSocketLog().addData("MailBoxData32", data);

    LogMock& mock = getCMessageQueueSocketLog();
    ASSERT_EQ(mock.popRecord(), "call sendRequest(0, 165, size=0 {}, state=2"); // getInfo

 */
class LogMock {
public:
    using LogRecords = std::list<std::string>;
    using NamedData = std::list<std::any>;

    LogMock()
        : m_logs{}
    {
    }

    /**
     * @brief Get stream for logging
     *
     * @return std::stringstream&
     */
    std::stringstream &log()
    {
        return m_log_stream;
    }

    /**
     * @brief get saved log record
     * must be saved by call saveLog()
     *
     * @return std::string
     */
    std::string popRecord()
    {
        std::string log{};
        if (!m_logs.empty()) {
            log = m_logs.front();
            m_logs.pop_front();
        }
        return log;
    }

    void clearLogs()
    {
        m_logs.clear();
        m_log_stream.str(std::string());
    }

    /**
     * @brief Log buffer items into stream
     * * must be saved by call saveLog()
     *
     * @tparam T
     * @param buffer
     * @param items_cnt
     */
    template<typename T>
    void logBuffer(const T* buffer, size_t items_cnt)
    {
        log() << std::dec << "items=" << items_cnt << " {";
        for (size_t i = 0; i < items_cnt; ++i) {
            log() << buffer[i] << ", ";
        }
        log() << "}";
    }

    /**
     * @brief Log buffer bytes into stream
     * * must be saved by call saveLog()
     *
     * @tparam T
     * @param buffer
     * @param items_cnt
     */
    template<typename T>
    void logBuffer8(const T* buffer, size_t items_cnt)
    {
        const std::uint8_t* buf8{reinterpret_cast<const std::uint8_t*>(buffer)};
        constexpr size_t kItemSize{sizeof(buffer[0])};
        const size_t buff8_size{items_cnt * kItemSize};

        log() << std::dec << "items=" << items_cnt << ", size=" << buff8_size << std::hex << " B 0x{"
              << std::setfill('0');
        for (size_t i = 0; i < items_cnt; ++i) {
            for (size_t bi = 0; bi < kItemSize; ++bi) {
                log() << std::setw(2) << static_cast<int>(buf8[bi + kItemSize * i]);
            }
            log() << " ";
        }
        log() << std::dec << "}";
    }

    /**
     * @brief Store the log stream and create record from it
     * so it can be retrieved later by popRecord()
     *
     */
    void saveLog()
    {
        m_logs.push_back(m_log_stream.str());
        m_log_stream.str(std::string());
    }

    /**
     * @brief User can add data for mocked functions
     * See implementation of a mocked function
     *
     * @tparam T    type autodeduction
     * @param name  name of data which will be stored (must match with the name in popData)
     * @param value value to store
     */
    template<typename T>
    void addData(const std::string &name, T value)
    {
        auto it = m_named_data.find(name);
        if (it == m_named_data.end()) {
            NamedData empty;
            auto res = m_named_data.insert({name, empty});
            it = res.first;
        }
        it->second.push_back(value);
    }

    /**
     * @brief return unused data
     *
     * @tparam T
     * @param name
     * @param value
     */
    template<typename T>
    void returnData(const std::string &name, T value)
    {
        auto it = m_named_data.find(name);
        if (it == m_named_data.end()) {
            NamedData empty;
            auto res = m_named_data.insert({name, empty});
            it = res.first;
        }
        it->second.push_front(value);
    }

    /**
     * @brief A mocked function can retrieve user data for mocking
     *
     * @tparam T        type autodeduction
     * @param name      name of the data
     * @param pop_var   variable where store the data
     * @return true     data exists and are stored
     * @return false    data does not exists
     */
    template<typename T>
    bool popData(const std::string &name, T* pop_var)
    {
        auto it = m_named_data.find(name);
        if (it == m_named_data.end()) {
            return false;
        }
        if (it->second.empty()) {
            return false;
        }
        *pop_var = std::any_cast<T>(it->second.front());
        it->second.pop_front();
        return true;
    }

    void clearData()
    {
        m_named_data.clear();
    }

    void printAllLogs(std::ostream &out)
    {
        for (auto log : m_logs) {
            out << log << "\n";
        }
    }

private:

    std::stringstream m_log_stream;
    LogRecords m_logs;
    std::map<std::string, NamedData> m_named_data;
};

#endif

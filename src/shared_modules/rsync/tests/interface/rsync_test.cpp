/*
 * Wazuh DBSYNC
 * Copyright (C) 2015-2020, Wazuh Inc.
 * August 28, 2020.
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation.
 */

#include <iostream>
#include "rsync_test.h"
#include "rsync.h"
#include "rsync.hpp"
#include "dbsync.h"
#include "dbsync.hpp"

constexpr auto DATABASE_TEMP {"TEMP.db"};
constexpr auto SQL_STMT_INFO
{
    R"(
    PRAGMA foreign_keys=OFF;
    BEGIN TRANSACTION;
    CREATE TABLE entry_path (path TEXT NOT NULL, inode_id INTEGER, mode INTEGER, last_event INTEGER, entry_type INTEGER, scanned INTEGER, options INTEGER, checksum TEXT NOT NULL, PRIMARY KEY(path));
    INSERT INTO entry_path VALUES('/boot/grub2/fonts/unicode.pf2',1,0,1596489273,0,1,131583,'96482cde495f716fcd66a71a601fbb905c13b426');
    INSERT INTO entry_path VALUES('/boot/grub2/grubenv',2,0,1596489273,0,1,131583,'e041159610c7ec18490345af13f7f49371b56893');
    INSERT INTO entry_path VALUES('/boot/grub2/i386-pc/datehook.mod',3,0,1596489273,0,1,131583,'f83bc87319566e270fcece2fae4910bc18fe7355');
    INSERT INTO entry_path VALUES('/boot/grub2/i386-pc/gcry_whirlpool.mod',4,0,1596489273,0,1,131583,'d59ffd58d107b9398ff5a809097f056b903b3c3e');
    INSERT INTO entry_path VALUES('/boot/grub2/i386-pc/gzio.mod',5,0,1596489273,0,1,131583,'e4a541bdcf17cb5435064881a1616befdc71f871');
    CREATE INDEX path_index ON entry_path (path);
    CREATE INDEX inode_index ON entry_path (inode_id);
    COMMIT;)"
};

class CallbackMock
{
public:
    CallbackMock() = default;
    ~CallbackMock() = default;
    MOCK_METHOD(void, callbackMock, (const std::string&), ());
};

static void callback(const void* data,
                     const size_t /*size*/,
                     void* ctx)
{
    CallbackMock* wrapper { reinterpret_cast<CallbackMock*>(ctx)};
    wrapper->callbackMock(reinterpret_cast<const char *>(data));
}

static void callbackRSyncWrapper(const void* payload, size_t size, void* userData)
{
    if (userData && payload)
    {
        std::function<void(const std::string&)>* callback { static_cast<std::function<void(const std::string&)>*>(userData) };
        std::string payloadStr;
        payloadStr.assign(reinterpret_cast<const char*>(payload), size);
        (*callback)(payloadStr);
    }
}

struct CJsonDeleter
{
    void operator()(char* json)
    {
        cJSON_free(json);
    }
    void operator()(cJSON* json)
    {
        cJSON_Delete(json);
    }
};

static void logFunction(const char* msg)
{
    if (msg)
    {
        std::cout << msg << std::endl;
    }
}

void RSyncTest::SetUp()
{
    rsync_initialize(&logFunction);
};

void RSyncTest::TearDown()
{
    EXPECT_NO_THROW(rsync_teardown());
};

TEST_F(RSyncTest, Initialization)
{
    const auto handle { rsync_create() };
    ASSERT_NE(nullptr, handle);
}

TEST_F(RSyncTest, startSyncWithInvalidParams)
{
    const auto dbsyncHandle { dbsync_create(HostType::AGENT, DbEngineType::SQLITE3, DATABASE_TEMP, SQL_STMT_INFO) };
    ASSERT_NE(nullptr, dbsyncHandle);

    const auto rsyncHandle { rsync_create() };
    ASSERT_NE(nullptr, rsyncHandle);

    const auto startConfigStmt
    {
        R"({"table":"entry_path"})"
    };
    const std::unique_ptr<cJSON, CJsonDeleter> jsSelect{ cJSON_Parse(startConfigStmt) };
    sync_callback_data_t callbackData { callback, nullptr };

    ASSERT_NE(0, rsync_start_sync(nullptr, dbsyncHandle, jsSelect.get(), {}));
    ASSERT_NE(0, rsync_start_sync(rsyncHandle, nullptr, jsSelect.get(), {}));
    ASSERT_NE(0, rsync_start_sync(rsyncHandle, dbsyncHandle, nullptr, {}));
    ASSERT_NE(0, rsync_start_sync(rsyncHandle, dbsyncHandle, jsSelect.get(), callbackData));
}

TEST_F(RSyncTest, startSyncWithoutExtraParams)
{
    const auto dbsyncHandle { dbsync_create(HostType::AGENT, DbEngineType::SQLITE3, DATABASE_TEMP, SQL_STMT_INFO) };
    ASSERT_NE(nullptr, dbsyncHandle);

    const auto rsyncHandle { rsync_create() };
    ASSERT_NE(nullptr, rsyncHandle);

    const auto startConfigStmt
    {
        R"({"table":"entry_path"})"
    };
    const std::unique_ptr<cJSON, CJsonDeleter> jsSelect{ cJSON_Parse(startConfigStmt) };

    ASSERT_NE(0, rsync_start_sync(rsyncHandle, dbsyncHandle, jsSelect.get(), {}));
}

TEST_F(RSyncTest, startSyncWithBadSelectQuery)
{
    const auto dbsyncHandle { dbsync_create(HostType::AGENT, DbEngineType::SQLITE3, DATABASE_TEMP, SQL_STMT_INFO) };
    ASSERT_NE(nullptr, dbsyncHandle);

    const auto rsyncHandle { rsync_create() };
    ASSERT_NE(nullptr, rsyncHandle);

    const auto startConfigStmt
    {
        R"({"table":"entry_path",
             "extra_parameters":
                [{
                    "first":{
                        "row_data_query_json":
                            {
                                "row_filter":"",
                                "distinct_opt":false,
                                "order_by_opt":"path ASC LIMIT 1",
                                "count_opt":1
                            },
                    }
                    "last":{
                        "row_data_query_json":
                            {
                                "column_list":["path"],
                                "row_filter":"",
                                "distinct_opt":false,
                                "order_by_opt":"path DESC LIMIT 1",
                                "count_opt":1
                            }
                    }
               }],
            })"
    };
    const std::unique_ptr<cJSON, CJsonDeleter> jsSelect{ cJSON_Parse(startConfigStmt) };

    ASSERT_NE(0, rsync_start_sync(rsyncHandle, dbsyncHandle, jsSelect.get(), {}));
}

TEST_F(RSyncTest, startSyncWithIntegrityClear)
{
    const auto sql
    {
        R"(
        PRAGMA foreign_keys=OFF;
        BEGIN TRANSACTION;
        CREATE TABLE entry_path (path TEXT NOT NULL, inode_id INTEGER, mode INTEGER, last_event INTEGER, entry_type INTEGER, scanned INTEGER, options INTEGER, checksum TEXT NOT NULL, PRIMARY KEY(path));
        COMMIT;)"
    };
    const auto dbsyncHandle { dbsync_create(HostType::AGENT, DbEngineType::SQLITE3, DATABASE_TEMP, sql) };
    ASSERT_NE(nullptr, dbsyncHandle);

    const auto rsyncHandle { rsync_create() };
    ASSERT_NE(nullptr, rsyncHandle);

    const auto expectedResult1
    {
        R"({"component":"test_component","data":{")"
    };

    const auto expectedResult2
    {
        R"("type":"integrity_clear"})"
    };

    const auto startConfigStmt
    {
        R"({"table":"entry_path",
            "first_query":
                {
                    "column_list":["path"],
                    "row_filter":"WHERE path is null",
                    "distinct_opt":false,
                    "order_by_opt":"path ASC",
                    "count_opt":1
                },
            "last_query":
                {
                    "column_list":["path"],
                    "row_filter":"WHERE path is null",
                    "distinct_opt":false,
                    "order_by_opt":"path DESC",
                    "count_opt":1
                },
            "component":"test_component",
            "index":"path",
            "last_event":"last_event",
            "checksum_field":"checksum",
            "range_checksum_query_json":
                {
                    "row_filter":"WHERE path BETWEEN '?' and '?' ORDER BY path",
                    "column_list":["path, checksum"],
                    "distinct_opt":false,
                    "order_by_opt":"",
                    "count_opt":100
                }
            })"
    };
    const std::unique_ptr<cJSON, CJsonDeleter> jsSelect{ cJSON_Parse(startConfigStmt) };

    const auto checkExpected
    {
        [&](const std::string& payload) -> ::testing::AssertionResult
        {
            auto retVal { ::testing::AssertionFailure() };
            // Necessary to avoid checking against "ID" which is defined as: time(nullptr)
            auto firstSegment  { payload.find(expectedResult1) };
            auto secondSegment { payload.find(expectedResult2) };
            if(std::string::npos != firstSegment && std::string::npos != secondSegment)
            {
                retVal = ::testing::AssertionSuccess();
            }
            return retVal;
        }
    };

    std::function<void(const std::string&)> callbackWrapper
    {
        [&](const std::string& payload)
        {
            EXPECT_PRED1(checkExpected, payload);
        }
    };
    sync_callback_data_t callbackData { callbackRSyncWrapper, &callbackWrapper };

    ASSERT_EQ(0, rsync_start_sync(rsyncHandle, dbsyncHandle, jsSelect.get(), callbackData));
}

TEST_F(RSyncTest, startSyncIntegrityGlobal)
{
    const auto dbsyncHandle { dbsync_create(HostType::AGENT, DbEngineType::SQLITE3, DATABASE_TEMP, SQL_STMT_INFO) };
    ASSERT_NE(nullptr, dbsyncHandle);

    const auto rsyncHandle { rsync_create() };
    ASSERT_NE(nullptr, rsyncHandle);

    const auto expectedResult1
    {
        R"({"component":"test_component","data":{"begin":"/boot/grub2/i386-pc/gzio.mod","checksum":"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","end":"/boot/grub2/fonts/unicode.pf2")"
    };

    const auto expectedResult2
    {
        R"("type":"integrity_check_global"})"
    };

    const auto startConfigStmt
    {
        R"({"table":"entry_path",
            "first_query":
                {
                    "column_list":["path"],
                    "row_filter":"",
                    "distinct_opt":false,
                    "order_by_opt":"path ASC",
                    "count_opt":1
                },
            "last_query":
                {
                    "column_list":["path"],
                    "row_filter":"",
                    "distinct_opt":false,
                    "order_by_opt":"path DESC",
                    "count_opt":1
                },
            "component":"test_component",
            "index":"path",
            "last_event":"last_event",
            "checksum_field":"checksum",
            "range_checksum_query_json":
                {
                    "row_filter":"WHERE path BETWEEN '?' and '?' ORDER BY path",
                    "column_list":["path, checksum"],
                    "distinct_opt":false,
                    "order_by_opt":"",
                    "count_opt":100
                }
            })"
    };
    const std::unique_ptr<cJSON, CJsonDeleter> jsSelect{ cJSON_Parse(startConfigStmt) };

    const auto checkExpected
    {
        [&](const std::string& payload) -> ::testing::AssertionResult
        {
            auto retVal { ::testing::AssertionFailure() };
            // Necessary to avoid checking against "ID" which is defined as: time(nullptr)
            auto firstSegment  { payload.find(expectedResult1) };
            auto secondSegment { payload.find(expectedResult2) };
            if(std::string::npos != firstSegment && std::string::npos != secondSegment)
            {
                retVal = ::testing::AssertionSuccess();
            }
            return retVal;
        }
    };

    std::function<void(const std::string&)> callbackWrapper
    {
        [&](const std::string& payload)
        {
            EXPECT_PRED1(checkExpected, payload);
        }
    };
    sync_callback_data_t callbackData { callbackRSyncWrapper, &callbackWrapper };

    ASSERT_EQ(0, rsync_start_sync(rsyncHandle, dbsyncHandle, jsSelect.get(), callbackData));

    dbsync_teardown();
}

TEST_F(RSyncTest, registerSyncId)
{
    const auto handle { rsync_create() };
    ASSERT_EQ(-1, rsync_register_sync_id(handle, nullptr, nullptr, nullptr, {}));
}

TEST_F(RSyncTest, pushMessage)
{
    const std::string buffer{"test buffer"};
    const auto handle { rsync_create() };
    ASSERT_NE(0, rsync_push_message(handle, nullptr, 1000));
    ASSERT_NE(0, rsync_push_message(handle, reinterpret_cast<const void*>(0x1000), 0));
    ASSERT_EQ(0, rsync_push_message(handle, reinterpret_cast<const void*>(buffer.data()), buffer.size()));
}

TEST_F(RSyncTest, CloseWithoutInitialization)
{   
    EXPECT_EQ(-1, rsync_close(nullptr));
}

TEST_F(RSyncTest, CloseCorrectInitialization)
{
    const auto handle { rsync_create() };
    ASSERT_NE(nullptr, handle);
    EXPECT_EQ(0, rsync_close(handle));
}

TEST_F(RSyncTest, RegisterAndPush)
{
    const auto handle_dbsync { dbsync_create(HostType::AGENT, DbEngineType::SQLITE3, DATABASE_TEMP, SQL_STMT_INFO) };
    ASSERT_NE(nullptr, handle_dbsync);

    const auto handle_rsync { rsync_create() };
    ASSERT_NE(nullptr, handle_rsync);

    const auto expectedResult1
    {
        R"({"component":"test_component","data":{"begin":"/boot/grub2/fonts/unicode.pf2","checksum":"2d567d2a180a96ad6b3ecd9ec7beae31d103d090280e7eaec8383ef27c8ab4a5","end":"/boot/grub2/grubenv","id":1,"tail":"/boot/grub2/i386-pc/datehook.mod"},"type":"integrity_check_left"})"
    };

    const auto expectedResult2
    {
        R"({"component":"test_component","data":{"begin":"/boot/grub2/i386-pc/datehook.mod","checksum":"cc933107bbe6c3eee784b74e180b9da2dbfa6766807aa1483257f055e52e4ca9","end":"/boot/grub2/i386-pc/gzio.mod","id":1},"type":"integrity_check_right"})"
    };
    
    const auto expectedResult3
    {
        R"({"component":"test_component","data":{"attributes":{"checksum":"96482cde495f716fcd66a71a601fbb905c13b426","entry_type":0,"inode_id":1,"last_event":1596489273,"mode":0,"options":131583,"path":"/boot/grub2/fonts/unicode.pf2","scanned":1},"index":"/boot/grub2/fonts/unicode.pf2","timestamp":1596489273},"type":"state"})"
    };

    const auto expectedResult4
    {
        R"({"component":"test_component","data":{"attributes":{"checksum":"e041159610c7ec18490345af13f7f49371b56893","entry_type":0,"inode_id":2,"last_event":1596489273,"mode":0,"options":131583,"path":"/boot/grub2/grubenv","scanned":1},"index":"/boot/grub2/grubenv","timestamp":1596489273},"type":"state"})"
    };

    const auto expectedResult5
    {
        R"({"component":"test_component","data":{"attributes":{"checksum":"e4a541bdcf17cb5435064881a1616befdc71f871","entry_type":0,"inode_id":5,"last_event":1596489273,"mode":0,"options":131583,"path":"/boot/grub2/i386-pc/gzio.mod","scanned":1},"index":"/boot/grub2/i386-pc/gzio.mod","timestamp":1596489273},"type":"state"})"
    };

    const auto expectedResult6
    {
        R"({"component":"test_component","data":{"attributes":{"checksum":"d59ffd58d107b9398ff5a809097f056b903b3c3e","entry_type":0,"inode_id":4,"last_event":1596489273,"mode":0,"options":131583,"path":"/boot/grub2/i386-pc/gcry_whirlpool.mod","scanned":1},"index":"/boot/grub2/i386-pc/gcry_whirlpool.mod","timestamp":1596489273},"type":"state"})"
    };

    const auto expectedResult7
    {
        R"({"component":"test_component","data":{"attributes":{"checksum":"f83bc87319566e270fcece2fae4910bc18fe7355","entry_type":0,"inode_id":3,"last_event":1596489273,"mode":0,"options":131583,"path":"/boot/grub2/i386-pc/datehook.mod","scanned":1},"index":"/boot/grub2/i386-pc/datehook.mod","timestamp":1596489273},"type":"state"})"
    };

    const auto registerConfigStmt
    {
        R"({"decoder_type":"JSON_RANGE",
            "table":"entry_path",
            "component":"test_component",
            "index":"path",
            "last_event":"last_event",
            "checksum_field":"checksum",
            "no_data_query_json":
                {
                    "row_filter":" ",
                    "column_list":["path, inode_id, mode, last_event, entry_type, scanned, options, checksum"],
                    "distinct_opt":false,
                    "order_by_opt":"",
                    "count_opt":100
                },
            "count_range_query_json":
                {
                    "row_filter":"WHERE path BETWEEN '?' and '?' ORDER BY path",
                    "count_field_name":"count",
                    "column_list":["count(*) AS count "],
                    "distinct_opt":false,
                    "order_by_opt":"",
                    "count_opt":100
                },
            "row_data_query_json":
                {
                    "row_filter":"WHERE path ='?'",
                    "column_list":["path, inode_id, mode, last_event, entry_type, scanned, options, checksum"],
                    "distinct_opt":false,
                    "order_by_opt":"",
                    "count_opt":100
                },
            "range_checksum_query_json":
                {
                    "row_filter":"WHERE path BETWEEN '?' and '?' ORDER BY path",
                    "column_list":["path, inode_id, mode, last_event, entry_type, scanned, options, checksum"],
                    "distinct_opt":false,
                    "order_by_opt":"",
                    "count_opt":100
                }
        })"
    };

    CallbackMock wrapper;
    
    sync_callback_data_t callbackData { callback, &wrapper };
    EXPECT_CALL(wrapper, callbackMock(expectedResult1)).Times(1);
    EXPECT_CALL(wrapper, callbackMock(expectedResult2)).Times(1);
    EXPECT_CALL(wrapper, callbackMock(expectedResult3)).Times(2);
    EXPECT_CALL(wrapper, callbackMock(expectedResult4)).Times(1);
    EXPECT_CALL(wrapper, callbackMock(expectedResult5)).Times(1);
    EXPECT_CALL(wrapper, callbackMock(expectedResult6)).Times(1);
    EXPECT_CALL(wrapper, callbackMock(expectedResult7)).Times(1);

    const std::unique_ptr<cJSON, CJsonDeleter> spRegisterConfigStmt{ cJSON_Parse(registerConfigStmt) };
    ASSERT_EQ(0, rsync_register_sync_id(handle_rsync, "test_id", handle_dbsync, spRegisterConfigStmt.get(), callbackData));
    
    std::string buffer1{R"(test_id checksum_fail {"begin":"/boot/grub2/fonts/unicode.pf2","end":"/boot/grub2/i386-pc/gzio.mod","id":1})"};

    ASSERT_EQ(0, rsync_push_message(handle_rsync, reinterpret_cast<const void*>(buffer1.data()), buffer1.size()));

    std::string buffer2{R"(test_id checksum_fail {"begin":"/boot/grub2/fonts/unicode.pf2","end":"/boot/grub2/fonts/unicode.pf2","id":1})"};
    ASSERT_EQ(0, rsync_push_message(handle_rsync, reinterpret_cast<const void*>(buffer2.data()), buffer2.size()));

    std::string buffer3{R"(test_id no_data {"begin":"/boot/grub2/fonts/unicode.pf2","end":"/boot/grub2/i386-pc/gzio.mod","id":1})"};
    ASSERT_EQ(0, rsync_push_message(handle_rsync, reinterpret_cast<const void*>(buffer3.data()), buffer3.size()));

    EXPECT_EQ(0, rsync_close(handle_rsync));

    dbsync_teardown();
}

TEST_F(RSyncTest, RegisterIncorrectQueryAndPush)
{
    const auto handle_dbsync { dbsync_create(HostType::AGENT, DbEngineType::SQLITE3, DATABASE_TEMP, SQL_STMT_INFO) };
    ASSERT_NE(nullptr, handle_dbsync);

    const auto handle_rsync { rsync_create() };
    ASSERT_NE(nullptr, handle_rsync);

    const auto registerConfigStmt
    {
        R"({"decoder_type":"JSON_RANGE",
            "table":"entry_path",
            "component":"test_component",
            "index":"path",
            "last_event":"last_event",
            "checksum_field":"checksum",
            "no_data_query_json":
                {
                    "row_filter":" ",
                    "column_list":["pathx, inode_id, mode, last_event, entry_type, scanned, options, checksum"],
                    "distinct_opt":false,
                    "order_by_opt":"",
                    "count_opt":100                    
                },
            "count_range_query_json":
                {
                    "row_filter":"WHEREx path BETWEEN '?' and '?' ORDER BY path",
                    "count_field_name":"count",
                    "column_list":["count(*) AS count "],
                    "distinct_opt":false,
                    "order_by_opt":"",
                    "count_opt":100                    
                },
            "row_data_query_json":
                {
                    "row_filter":"WHEREx path ='?'",
                    "column_list":["path, inode_id, mode, last_event, entry_type, scanned, options, checksum"],
                    "distinct_opt":false,
                    "order_by_opt":"",
                    "count_opt":100                    
                },
            "range_checksum_query_json":
                {
                    "row_filter":"WHEREx path BETWEEN '?' and '?' ORDER BY path",
                    "column_list":["path, inode_id, mode, last_event, entry_type, scanned, options, checksum"],
                    "distinct_opt":false,
                    "order_by_opt":"",
                    "count_opt":100                    
                }
        })"
    };

    CallbackMock wrapper;
    
    sync_callback_data_t callbackData { callback, &wrapper };

    const std::unique_ptr<cJSON, CJsonDeleter> spRegisterConfigStmt{ cJSON_Parse(registerConfigStmt) };
    ASSERT_EQ(0, rsync_register_sync_id(handle_rsync, "test_id", handle_dbsync, spRegisterConfigStmt.get(), callbackData));
    
    std::string buffer1{R"(test_id checksum_fail {"begin":"/boot/grub2/fonts/unicode.pf2","end":"/boot/grub2/i386-pc/gzio.mod","id":1})"};

    ASSERT_EQ(0, rsync_push_message(handle_rsync, reinterpret_cast<const void*>(buffer1.data()), buffer1.size()));

    std::string buffer2{R"(test_id checksum_fail {"begin":"/boot/grub2/fonts/unicode.pf2","end":"/boot/grub2/fonts/unicode.pf2","id":1})"};
    ASSERT_EQ(0, rsync_push_message(handle_rsync, reinterpret_cast<const void*>(buffer2.data()), buffer2.size()));

    std::string buffer3{R"(test_id no_data {"begin":"/boot/grub2/fonts/unicode.pf2","end":"/boot/grub2/i386-pc/gzio.mod","id":1})"};
    ASSERT_EQ(0, rsync_push_message(handle_rsync, reinterpret_cast<const void*>(buffer3.data()), buffer3.size()));

    EXPECT_EQ(0, rsync_close(handle_rsync));

    dbsync_teardown();
}



TEST_F(RSyncTest, RegisterAndPushCPP)
{
    std::unique_ptr<DBSync> dbSync;
    EXPECT_NO_THROW(dbSync = std::make_unique<DBSync>(HostType::AGENT, DbEngineType::SQLITE3, DATABASE_TEMP, SQL_STMT_INFO));

    std::unique_ptr<RemoteSync> remoteSync;
    EXPECT_NO_THROW(remoteSync = std::make_unique<RemoteSync>());
    
    const auto expectedResult1
    {
        R"({"component":"test_component","data":{"begin":"/boot/grub2/fonts/unicode.pf2","checksum":"2d567d2a180a96ad6b3ecd9ec7beae31d103d090280e7eaec8383ef27c8ab4a5","end":"/boot/grub2/grubenv","id":1,"tail":"/boot/grub2/i386-pc/datehook.mod"},"type":"integrity_check_left"})"
    };

    const auto expectedResult2
    {
        R"({"component":"test_component","data":{"begin":"/boot/grub2/i386-pc/datehook.mod","checksum":"cc933107bbe6c3eee784b74e180b9da2dbfa6766807aa1483257f055e52e4ca9","end":"/boot/grub2/i386-pc/gzio.mod","id":1},"type":"integrity_check_right"})"
    };
    
    const auto expectedResult3
    {
        R"({"component":"test_component","data":{"attributes":{"checksum":"96482cde495f716fcd66a71a601fbb905c13b426","entry_type":0,"inode_id":1,"last_event":1596489273,"mode":0,"options":131583,"path":"/boot/grub2/fonts/unicode.pf2","scanned":1},"index":"/boot/grub2/fonts/unicode.pf2","timestamp":1596489273},"type":"state"})"
    };

    const auto expectedResult4
    {
        R"({"component":"test_component","data":{"attributes":{"checksum":"e041159610c7ec18490345af13f7f49371b56893","entry_type":0,"inode_id":2,"last_event":1596489273,"mode":0,"options":131583,"path":"/boot/grub2/grubenv","scanned":1},"index":"/boot/grub2/grubenv","timestamp":1596489273},"type":"state"})"
    };

    const auto expectedResult5
    {
        R"({"component":"test_component","data":{"attributes":{"checksum":"e4a541bdcf17cb5435064881a1616befdc71f871","entry_type":0,"inode_id":5,"last_event":1596489273,"mode":0,"options":131583,"path":"/boot/grub2/i386-pc/gzio.mod","scanned":1},"index":"/boot/grub2/i386-pc/gzio.mod","timestamp":1596489273},"type":"state"})"
    };

    const auto expectedResult6
    {
        R"({"component":"test_component","data":{"attributes":{"checksum":"d59ffd58d107b9398ff5a809097f056b903b3c3e","entry_type":0,"inode_id":4,"last_event":1596489273,"mode":0,"options":131583,"path":"/boot/grub2/i386-pc/gcry_whirlpool.mod","scanned":1},"index":"/boot/grub2/i386-pc/gcry_whirlpool.mod","timestamp":1596489273},"type":"state"})"
    };

    const auto expectedResult7
    {
        R"({"component":"test_component","data":{"attributes":{"checksum":"f83bc87319566e270fcece2fae4910bc18fe7355","entry_type":0,"inode_id":3,"last_event":1596489273,"mode":0,"options":131583,"path":"/boot/grub2/i386-pc/datehook.mod","scanned":1},"index":"/boot/grub2/i386-pc/datehook.mod","timestamp":1596489273},"type":"state"})"
    };

    const auto registerConfigStmt
    {
        R"({"decoder_type":"JSON_RANGE",
            "table":"entry_path",
            "component":"test_component",
            "index":"path",
            "last_event":"last_event",
            "checksum_field":"checksum",
            "no_data_query_json":
                {
                    "row_filter":" ",
                    "column_list":["path, inode_id, mode, last_event, entry_type, scanned, options, checksum"],
                    "distinct_opt":false,
                    "order_by_opt":"",
                    "count_opt":100
                },
            "count_range_query_json":
                {
                    "row_filter":"WHERE path BETWEEN '?' and '?' ORDER BY path",
                    "count_field_name":"count",
                    "column_list":["count(*) AS count "],
                    "distinct_opt":false,
                    "order_by_opt":"",
                    "count_opt":100
                },
            "row_data_query_json":
                {
                    "row_filter":"WHERE path ='?'",
                    "column_list":["path, inode_id, mode, last_event, entry_type, scanned, options, checksum"],
                    "distinct_opt":false,
                    "order_by_opt":"",
                    "count_opt":100
                },
            "range_checksum_query_json":
                {
                    "row_filter":"WHERE path BETWEEN '?' and '?' ORDER BY path",
                    "column_list":["path, inode_id, mode, last_event, entry_type, scanned, options, checksum"],
                    "distinct_opt":false,
                    "order_by_opt":"",
                    "count_opt":100
                }
        })"
    };

    CallbackMock wrapper;
    SyncCallbackData callbackData 
    { 
        [&wrapper](const std::string& data)
        {
            wrapper.callbackMock(data);
        } 
    };
    EXPECT_CALL(wrapper, callbackMock(expectedResult1)).Times(1);
    EXPECT_CALL(wrapper, callbackMock(expectedResult2)).Times(1);
    EXPECT_CALL(wrapper, callbackMock(expectedResult3)).Times(2);
    EXPECT_CALL(wrapper, callbackMock(expectedResult4)).Times(1);
    EXPECT_CALL(wrapper, callbackMock(expectedResult5)).Times(1);
    EXPECT_CALL(wrapper, callbackMock(expectedResult6)).Times(1);
    EXPECT_CALL(wrapper, callbackMock(expectedResult7)).Times(1);

    ASSERT_NO_THROW(remoteSync->registerSyncID("test_id", dbSync->handle(), nlohmann::json::parse(registerConfigStmt), callbackData));
    
    std::string buffer1{R"(test_id checksum_fail {"begin":"/boot/grub2/fonts/unicode.pf2","end":"/boot/grub2/i386-pc/gzio.mod","id":1})"};

    ASSERT_NO_THROW(remoteSync->pushMessage({ buffer1.begin(), buffer1.end() }));

    std::string buffer2{R"(test_id checksum_fail {"begin":"/boot/grub2/fonts/unicode.pf2","end":"/boot/grub2/fonts/unicode.pf2","id":1})"};
    ASSERT_NO_THROW(remoteSync->pushMessage({ buffer2.begin(), buffer2.end() }));

    std::string buffer3{R"(test_id no_data {"begin":"/boot/grub2/fonts/unicode.pf2","end":"/boot/grub2/i386-pc/gzio.mod","id":1})"};
    ASSERT_NO_THROW(remoteSync->pushMessage({ buffer3.begin(), buffer3.end() }));

    //sleep(1);
    remoteSync.reset();
}

TEST_F(RSyncTest, startSyncWithIntegrityClearCPP)
{
    const auto sql
    {
        R"(
        PRAGMA foreign_keys=OFF;
        BEGIN TRANSACTION;
        CREATE TABLE entry_path (path TEXT NOT NULL, inode_id INTEGER, mode INTEGER, last_event INTEGER, entry_type INTEGER, scanned INTEGER, options INTEGER, checksum TEXT NOT NULL, PRIMARY KEY(path));
        COMMIT;)"
    };

    std::unique_ptr<DBSync> dbSync;
    EXPECT_NO_THROW(dbSync = std::make_unique<DBSync>(HostType::AGENT, DbEngineType::SQLITE3, DATABASE_TEMP, sql));

    std::unique_ptr<RemoteSync> remoteSync;
    EXPECT_NO_THROW(remoteSync = std::make_unique<RemoteSync>());

    const auto expectedResult1
    {
        R"({"component":"test_component","data":{")"
    };

    const auto expectedResult2
    {
        R"("type":"integrity_clear"})"
    };

    const auto startConfigStmt
    {
        R"({"table":"entry_path",
            "first_query":
                {
                    "column_list":["path"],
                    "row_filter":"WHERE path is null",
                    "distinct_opt":false,
                    "order_by_opt":"path ASC",
                    "count_opt":1
                },
            "last_query":
                {
                    "column_list":["path"],
                    "row_filter":"WHERE path is null",
                    "distinct_opt":false,
                    "order_by_opt":"path DESC",
                    "count_opt":1
                },
            "component":"test_component",
            "index":"path",
            "last_event":"last_event",
            "checksum_field":"checksum",
            "range_checksum_query_json":
                {
                    "row_filter":"WHERE path BETWEEN '?' and '?' ORDER BY path",
                    "column_list":["path, checksum"],
                    "distinct_opt":false,
                    "order_by_opt":"",
                    "count_opt":100
                }
            })"
    };

    const auto checkExpected
    {
        [&](const std::string& payload) -> ::testing::AssertionResult
        {
            auto retVal { ::testing::AssertionFailure() };
            // Necessary to avoid checking against "ID" which is defined as: time(nullptr)
            auto firstSegment  { payload.find(expectedResult1) };
            auto secondSegment { payload.find(expectedResult2) };
            if(std::string::npos != firstSegment && std::string::npos != secondSegment)
            {
                retVal = ::testing::AssertionSuccess();
            }
            return retVal;
        }
    };

    std::function<void(const std::string&)> callbackWrapper
    {
        [&](const std::string& payload)
        {
            EXPECT_PRED1(checkExpected, payload);
        }
    };

    SyncCallbackData callbackData 
    { 
        [&callbackWrapper](const std::string& payload)
        {
            callbackWrapper(payload);
        }
    };

    EXPECT_NO_THROW(remoteSync->startSync(dbSync->handle(), nlohmann::json::parse(startConfigStmt), callbackData));
}

TEST_F(RSyncTest, startSyncWithIntegrityClearCPPSelectByInode)
{
    const auto sql
    {
        R"(
        PRAGMA foreign_keys=OFF;
        BEGIN TRANSACTION;
        CREATE TABLE entry_path (path TEXT NOT NULL, inode_id INTEGER, mode INTEGER, last_event INTEGER, entry_type INTEGER, scanned INTEGER, options INTEGER, checksum TEXT NOT NULL, PRIMARY KEY(path));
        COMMIT;)"
    };

    std::unique_ptr<DBSync> dbSync;
    EXPECT_NO_THROW(dbSync = std::make_unique<DBSync>(HostType::AGENT, DbEngineType::SQLITE3, DATABASE_TEMP, SQL_STMT_INFO));

    std::unique_ptr<RemoteSync> remoteSync;
    EXPECT_NO_THROW(remoteSync = std::make_unique<RemoteSync>());

    const auto startConfigStmt
    {
        R"({"table":"entry_path",
            "first_query":
                {
                    "column_list":["inode_id"],
                    "row_filter":" ",
                    "distinct_opt":false,
                    "order_by_opt":"inode_id ASC",
                    "count_opt":1
                },
            "last_query":
                {
                    "column_list":["inode_id"],
                    "row_filter":" ",
                    "distinct_opt":false,
                    "order_by_opt":"inode_id DESC",
                    "count_opt":1
                },
            "component":"test_component",
            "index":"inode_id",
            "last_event":"last_event",
            "checksum_field":"checksum",
            "range_checksum_query_json":
                {
                    "row_filter":"WHERE inode_id BETWEEN '?' and '?' ORDER BY inode_id",
                    "column_list":["inode_id, checksum"],
                    "distinct_opt":false,
                    "order_by_opt":"",
                    "count_opt":100
                }
            })"
    };

    std::function<void(const std::string&)> callbackWrapper
    {
        [&](const std::string& payload)
        {
            EXPECT_FALSE(payload.empty());
        }
    };

    SyncCallbackData callbackData
    {
        [&callbackWrapper](const std::string& payload)
        {
            callbackWrapper(payload);
        }
    };

    EXPECT_NO_THROW(remoteSync->startSync(dbSync->handle(), nlohmann::json::parse(startConfigStmt), callbackData));
}


TEST_F(RSyncTest, constructorWithHandle)
{
    std::unique_ptr<RemoteSync> remoteSync;
    EXPECT_NO_THROW(remoteSync = std::make_unique<RemoteSync>());

    std::unique_ptr<RemoteSync> remoteSyncHandled;
    EXPECT_NO_THROW(remoteSyncHandled = std::make_unique<RemoteSync>(remoteSync->handle()));

    EXPECT_EQ(remoteSync->handle(), remoteSyncHandled->handle());
    
}

TEST_F(RSyncTest, teardown)
{
    EXPECT_NO_THROW(RemoteSync::teardown());
}
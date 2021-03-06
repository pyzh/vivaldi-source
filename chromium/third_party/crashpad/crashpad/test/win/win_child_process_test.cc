// Copyright 2015 The Crashpad Authors. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "test/win/win_child_process.h"

#include <windows.h>
#include <stdlib.h>

#include "base/macros.h"
#include "gtest/gtest.h"

namespace crashpad {
namespace test {
namespace {

int ReadInt(HANDLE handle) {
  int value = 0;
  DWORD bytes_read = 0;
  EXPECT_TRUE(::ReadFile(handle, &value, sizeof(value), &bytes_read, nullptr));
  EXPECT_EQ(sizeof(value), bytes_read);
  return value;
}

void WriteInt(HANDLE handle, int value) {
  DWORD bytes_written = 0;
  EXPECT_TRUE(
      ::WriteFile(handle, &value, sizeof(value), &bytes_written, nullptr));
  EXPECT_EQ(sizeof(value), bytes_written);
}

class TestWinChildProcess final : public WinChildProcess {
 public:
  TestWinChildProcess() : WinChildProcess() {}

  ~TestWinChildProcess() {}

 private:
  // WinChildProcess will have already exercised the pipes.
  int Run() override {
    int value = ReadInt(ReadPipeHandle());
    WriteInt(WritePipeHandle(), value);
    return testing::Test::HasFailure() ? EXIT_FAILURE : EXIT_SUCCESS;
  }

  DISALLOW_COPY_AND_ASSIGN(TestWinChildProcess);
};

TEST(WinChildProcessTest, WinChildProcess) {
  WinChildProcess::EntryPoint<TestWinChildProcess>();

  scoped_ptr<WinChildProcess::Handles> handles = WinChildProcess::Launch();
  WriteInt(handles->write.get(), 1);
  ASSERT_EQ(1, ReadInt(handles->read.get()));
}

TEST(WinChildProcessTest, MultipleChildren) {
  WinChildProcess::EntryPoint<TestWinChildProcess>();

  scoped_ptr<WinChildProcess::Handles> handles_1 = WinChildProcess::Launch();
  scoped_ptr<WinChildProcess::Handles> handles_2 = WinChildProcess::Launch();
  scoped_ptr<WinChildProcess::Handles> handles_3 = WinChildProcess::Launch();

  WriteInt(handles_1->write.get(), 1);
  WriteInt(handles_2->write.get(), 2);
  WriteInt(handles_3->write.get(), 3);
  ASSERT_EQ(3, ReadInt(handles_3->read.get()));
  ASSERT_EQ(2, ReadInt(handles_2->read.get()));
  ASSERT_EQ(1, ReadInt(handles_1->read.get()));
}

}  // namespace
}  // namespace test
}  // namespace crashpad

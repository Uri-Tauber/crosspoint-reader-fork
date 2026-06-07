#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <cstring>

namespace {

static constexpr size_t BUFFER_SIZE = 4096;

// Mock HalFile for testing
struct MockHalFile {
  std::string buffer;
  bool is_open = true;

  operator bool() const { return is_open; }
  void close() { is_open = false; }
  
  size_t write(const uint8_t* buf, size_t size) {
    if (!is_open) return 0;
    buffer.append(reinterpret_cast<const char*>(buf), size);
    return size;
  }
};

struct UploadBuffer {
  MockHalFile file;
  std::vector<uint8_t> buffer;
  size_t bufferPos = 0;
  bool failNextWrite = false;

  void init() {
    file.buffer.clear();
    file.is_open = true;
    buffer.resize(BUFFER_SIZE);
    bufferPos = 0;
    failNextWrite = false;
  }
};

bool flushBuffer(UploadBuffer& state) {
  if (state.bufferPos > 0 && state.file) {
    if (state.failNextWrite) {
      state.bufferPos = 0;
      return false;
    }
    const size_t written = state.file.write(state.buffer.data(), state.bufferPos);
    if (written != state.bufferPos) {
      state.bufferPos = 0;
      return false;
    }
    state.bufferPos = 0;
  }
  return true;
}

bool writeChunk(UploadBuffer& state, const uint8_t* data, size_t size) {
  size_t remaining = size;
  while (remaining > 0) {
    size_t space = BUFFER_SIZE - state.bufferPos;
    size_t toCopy = remaining < space ? remaining : space;
    memcpy(state.buffer.data() + state.bufferPos, data, toCopy);
    state.bufferPos += toCopy;
    data += toCopy;
    remaining -= toCopy;

    if (state.bufferPos >= BUFFER_SIZE) {
      if (!flushBuffer(state)) {
        return false;
      }
    }
  }
  return true;
}

std::vector<uint8_t> makeData(size_t size) {
  std::vector<uint8_t> data(size);
  for (size_t i = 0; i < size; i++) {
    data[i] = static_cast<uint8_t>(i & 0xFF);
  }
  return data;
}

} // namespace

class UploadBufferTest : public ::testing::Test {
protected:
  UploadBuffer state;
  void SetUp() override {
    state.init();
  }
};

TEST_F(UploadBufferTest, FlushEmptySucceeds) {
  EXPECT_TRUE(flushBuffer(state));
  EXPECT_TRUE(state.file.buffer.empty());
}

TEST_F(UploadBufferTest, FlushWithDataSucceeds) {
  state.buffer[0] = 0xAA;
  state.buffer[1] = 0xBB;
  state.bufferPos = 2;
  EXPECT_TRUE(flushBuffer(state));
  EXPECT_EQ(state.bufferPos, 0);
  EXPECT_EQ(state.file.buffer.size(), 2);
}

TEST_F(UploadBufferTest, WriteSmallChunk) {
  auto data = makeData(100);
  EXPECT_TRUE(writeChunk(state, data.data(), data.size()));
  EXPECT_EQ(state.bufferPos, 100);
  EXPECT_TRUE(state.file.buffer.empty());
  flushBuffer(state);
  EXPECT_EQ(state.file.buffer.size(), 100);
}

TEST_F(UploadBufferTest, WriteExactBuffer) {
  auto data = makeData(BUFFER_SIZE);
  EXPECT_TRUE(writeChunk(state, data.data(), data.size()));
  EXPECT_EQ(state.bufferPos, 0);
  EXPECT_EQ(state.file.buffer.size(), BUFFER_SIZE);
}

TEST_F(UploadBufferTest, WriteOverflow) {
  size_t dataSize = BUFFER_SIZE + 500;
  auto data = makeData(dataSize);
  EXPECT_TRUE(writeChunk(state, data.data(), data.size()));
  EXPECT_EQ(state.bufferPos, 500);
  EXPECT_EQ(state.file.buffer.size(), BUFFER_SIZE);
  flushBuffer(state);
  EXPECT_EQ(state.file.buffer.size(), dataSize);
}

TEST_F(UploadBufferTest, DataIntegrity) {
  auto data = makeData(BUFFER_SIZE + 100);
  writeChunk(state, data.data(), data.size());
  flushBuffer(state);
  EXPECT_EQ(state.file.buffer.size(), data.size());
  bool match = true;
  for (size_t i = 0; i < data.size(); i++) {
    if (data[i] != static_cast<uint8_t>(state.file.buffer[i])) {
      match = false;
      break;
    }
  }
  EXPECT_TRUE(match);
}

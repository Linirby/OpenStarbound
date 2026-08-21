#include <iostream>
#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>

// Standalone verification test for Uniform Buffer Object (UBO) / std140 layout packing

namespace Star {

class TestByteArray {
public:
  void writeFrom(char const* data, size_t pos, size_t len) {
    if (pos + len > m_data.size())
      m_data.resize(pos + len, 0);
    std::memcpy(m_data.data() + pos, data, len);
  }

  void append(char const* data, size_t len) {
    size_t oldSize = m_data.size();
    m_data.resize(oldSize + len);
    std::memcpy(m_data.data() + oldSize, data, len);
  }

  char const* ptr() const { return m_data.data(); }
  size_t size() const { return m_data.size(); }

private:
  std::vector<char> m_data;
};

static void writeAligned(TestByteArray& out, size_t& offset, void const* data, size_t size, size_t alignment) {
  offset = (offset + (alignment - 1)) & ~(alignment - 1);
  out.writeFrom((char const*)data, offset, size);
  offset += size;
}

struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };
struct Vec4 { float x, y, z, w; };

// Target std140 C++ structs for binary comparison
struct TestBlock1 {
  float a;         // offset 0, size 4
  float _pad0;     // offset 4, padding 4
  Vec2 b;          // offset 8, size 8
  float c;         // offset 16, size 4
  float _pad1[3];  // offset 20, padding 12
  Vec4 d;          // offset 32, size 16
};

struct TestBlockVec3Float {
  Vec3 v;          // offset 0, size 12
  float f;         // offset 12, size 4 (tight packing in same 16-byte slot)
};

}

int main() {
  using namespace Star;

  std::cout << "========================================" << std::endl;
  std::cout << "Running Uniform Buffer (UBO) std140 Tests" << std::endl;
  std::cout << "========================================" << std::endl;

  // Test 1: Scalar alignment & writing
  {
    TestByteArray buf;
    size_t offset = 0;
    float f = 3.14159f;
    int32_t i = 42;
    int32_t b = 1;

    writeAligned(buf, offset, &f, sizeof(f), 4);
    writeAligned(buf, offset, &i, sizeof(i), 4);
    writeAligned(buf, offset, &b, sizeof(b), 4);

    assert(offset == 12);
    assert(buf.size() == 12);

    float fOut; int32_t iOut, bOut;
    std::memcpy(&fOut, buf.ptr() + 0, 4);
    std::memcpy(&iOut, buf.ptr() + 4, 4);
    std::memcpy(&bOut, buf.ptr() + 8, 4);

    assert(std::abs(fOut - 3.14159f) < 0.0001f);
    assert(iOut == 42);
    assert(bOut == 1);
    std::cout << "[PASS] Test 1: Scalar std140 4-byte alignment & values" << std::endl;
  }

  // Test 2: Vec2 8-byte alignment
  {
    TestByteArray buf;
    size_t offset = 0;
    float scalar = 1.0f;
    Vec2 v{2.0f, 3.0f};

    writeAligned(buf, offset, &scalar, sizeof(scalar), 4); // offset -> 4
    assert(offset == 4);

    writeAligned(buf, offset, &v, sizeof(v), 8);           // offset -> aligned 8 -> 16
    assert(offset == 16);

    float sOut;
    Vec2 vOut;
    std::memcpy(&sOut, buf.ptr() + 0, 4);
    std::memcpy(&vOut, buf.ptr() + 8, 8);

    assert(sOut == 1.0f);
    assert(vOut.x == 2.0f && vOut.y == 3.0f);
    std::cout << "[PASS] Test 2: Vec2 8-byte alignment & 4-byte padding" << std::endl;
  }

  // Test 3: Vec3 16-byte alignment and scalar tight packing
  {
    TestByteArray buf;
    size_t offset = 0;
    Vec3 v3{10.0f, 20.0f, 30.0f};
    float trail = 40.0f;

    writeAligned(buf, offset, &v3, sizeof(v3), 16); // offset -> 12
    assert(offset == 12);

    writeAligned(buf, offset, &trail, sizeof(trail), 4); // offset -> 16
    assert(offset == 16);

    TestBlockVec3Float expected{v3, trail};
    assert(std::memcmp(buf.ptr(), &expected, sizeof(expected)) == 0);
    std::cout << "[PASS] Test 3: Vec3 16-byte alignment + scalar tight packing" << std::endl;
  }

  // Test 4: Mixed struct layout matching C++ definition (48 bytes total)
  {
    TestByteArray buf;
    size_t offset = 0;
    float a = 1.5f;
    Vec2 b{2.5f, 3.5f};
    float c = 4.5f;
    Vec4 d{5.5f, 6.5f, 7.5f, 8.5f};

    writeAligned(buf, offset, &a, sizeof(a), 4);   // 0 -> 4
    writeAligned(buf, offset, &b, sizeof(b), 8);   // 4 -> pad to 8 -> 16
    writeAligned(buf, offset, &c, sizeof(c), 4);   // 16 -> 20
    writeAligned(buf, offset, &d, sizeof(d), 16);  // 20 -> pad to 32 -> 48

    assert(offset == 48);
    assert(buf.size() == 48);

    TestBlock1 expected{};
    expected.a = a;
    expected.b = b;
    expected.c = c;
    expected.d = d;

    assert(std::memcmp(buf.ptr() + 0, &expected.a, sizeof(float)) == 0);
    assert(std::memcmp(buf.ptr() + 8, &expected.b, sizeof(Vec2)) == 0);
    assert(std::memcmp(buf.ptr() + 16, &expected.c, sizeof(float)) == 0);
    assert(std::memcmp(buf.ptr() + 32, &expected.d, sizeof(Vec4)) == 0);
    std::cout << "[PASS] Test 4: Mixed complex struct layout (48 bytes)" << std::endl;
  }

  // Test 5: Mat3 3x3 matrix column-major packing (48 bytes total)
  {
    TestByteArray buf;
    size_t offset = 0;
    float mat[9] = {
      1.0f, 2.0f, 3.0f, // column 0
      4.0f, 5.0f, 6.0f, // column 1
      7.0f, 8.0f, 9.0f  // column 2
    };

    offset = (offset + 15) & ~15;
    for (size_t col = 0; col < 3; ++col) {
      writeAligned(buf, offset, &mat[col * 3], sizeof(float) * 3, 16);
      offset = (offset + 15) & ~15; // each column padded to 16 bytes
    }

    assert(offset == 48);
    assert(buf.size() >= 44);

    float c0[3], c1[3], c2[3];
    std::memcpy(c0, buf.ptr() + 0, 12);
    std::memcpy(c1, buf.ptr() + 16, 12);
    std::memcpy(c2, buf.ptr() + 32, 12);

    assert(c0[0] == 1.0f && c0[1] == 2.0f && c0[2] == 3.0f);
    assert(c1[0] == 4.0f && c1[1] == 5.0f && c1[2] == 6.0f);
    assert(c2[0] == 7.0f && c2[1] == 8.0f && c2[2] == 9.0f);
    std::cout << "[PASS] Test 5: Mat3 3-column std140 packing (48 bytes)" << std::endl;
  }

  // Test 6: Raw binary byte stream passthrough
  {
    TestByteArray buf;
    char rawData[] = "RAW_BINARY_DATA_TEST_123456789";
    buf.append(rawData, sizeof(rawData));

    assert(buf.size() == sizeof(rawData));
    assert(std::memcmp(buf.ptr(), rawData, sizeof(rawData)) == 0);
    std::cout << "[PASS] Test 6: Raw byte string passthrough" << std::endl;
  }

  std::cout << "========================================" << std::endl;
  std::cout << "ALL 6 UNIFORM BUFFER TESTS PASSED!" << std::endl;
  std::cout << "========================================" << std::endl;

  return 0;
}

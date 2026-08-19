#include "PolygonClippingImpl.h"

#include "core/polyclip.h"

#include <cstdint>
#include <stdexcept>

namespace facebook::react {

namespace {

const char* kBadEncoding = "PolygonClipping: malformed geometry encoding";

/* Flat encoding shared with src/NativePolygonClipping.ts:
 *   geometry  := numMultiPolys multiPoly*
 *   multiPoly := numPolys poly*
 *   poly      := numRings ring*
 *   ring      := numPairs (x y)*
 */
class Reader {
 public:
  explicit Reader(const std::vector<double>& data) : data_(data) {}

  int64_t count() {
    if (pos_ >= data_.size()) throw std::invalid_argument(kBadEncoding);
    const double v = data_[pos_++];
    const int64_t n = static_cast<int64_t>(v);
    if (n < 0 || static_cast<double>(n) != v || n > static_cast<int64_t>(data_.size())) {
      throw std::invalid_argument(kBadEncoding);
    }
    return n;
  }

  void coords(std::vector<double>& out, int64_t pairs) {
    const size_t n = static_cast<size_t>(pairs) * 2;
    if (pos_ + n > data_.size()) throw std::invalid_argument(kBadEncoding);
    out.insert(out.end(), data_.begin() + static_cast<int64_t>(pos_),
               data_.begin() + static_cast<int64_t>(pos_ + n));
    pos_ += n;
  }

  bool done() const { return pos_ == data_.size(); }

 private:
  const std::vector<double>& data_;
  size_t pos_ = 0;
};

polyclip::PackedMultiPolygon readMultiPoly(Reader& reader) {
  polyclip::PackedMultiPolygon mp;
  const int64_t numPolys = reader.count();
  mp.reserve(static_cast<size_t>(numPolys));
  for (int64_t p = 0; p < numPolys; p++) {
    polyclip::PackedPolygon poly;
    const int64_t numRings = reader.count();
    poly.ringLengths.reserve(static_cast<size_t>(numRings));
    for (int64_t ring = 0; ring < numRings; ring++) {
      const int64_t pairs = reader.count();
      poly.ringLengths.push_back(static_cast<int32_t>(pairs));
      reader.coords(poly.coords, pairs);
    }
    mp.push_back(std::move(poly));
  }
  return mp;
}

size_t encodedMultiPolySize(const polyclip::PackedMultiPolygon& mp) {
  size_t size = 1;
  for (const auto& poly : mp) {
    size += 1 + poly.ringLengths.size() + poly.coords.size();
  }
  return size;
}

void appendMultiPoly(std::vector<double>& out, const polyclip::PackedMultiPolygon& mp) {
  out.push_back(static_cast<double>(mp.size()));
  for (const auto& poly : mp) {
    out.push_back(static_cast<double>(poly.ringLengths.size()));
    size_t coordIndex = 0;
    for (const int32_t pairs : poly.ringLengths) {
      out.push_back(static_cast<double>(pairs));
      const size_t n = static_cast<size_t>(pairs) * 2;
      out.insert(out.end(), poly.coords.begin() + static_cast<int64_t>(coordIndex),
                 poly.coords.begin() + static_cast<int64_t>(coordIndex + n));
      coordIndex += n;
    }
  }
}

std::vector<double> writeMultiPoly(const polyclip::PackedMultiPolygon& mp) {
  std::vector<double> out;
  out.reserve(encodedMultiPolySize(mp));
  appendMultiPoly(out, mp);
  return out;
}

polyclip::OpType opFromDouble(double op) {
  switch (static_cast<int>(op)) {
    case 0:
      return polyclip::OpType::Union;
    case 1:
      return polyclip::OpType::Intersection;
    case 2:
      return polyclip::OpType::Xor;
    case 3:
      return polyclip::OpType::Difference;
    default:
      throw std::invalid_argument("PolygonClipping: unknown operation");
  }
}

} // namespace

PolygonClippingImpl::PolygonClippingImpl(std::shared_ptr<CallInvoker> jsInvoker)
    : NativePolygonClippingCxxSpec(std::move(jsInvoker)) {}

std::vector<double> PolygonClippingImpl::clip(jsi::Runtime& rt, double op, std::vector<double> geometry) {
  try {
    const polyclip::OpType opType = opFromDouble(op);
    Reader reader(geometry);
    const int64_t numMultiPolys = reader.count();
    if (numMultiPolys < 1) throw std::invalid_argument(kBadEncoding);
    const polyclip::PackedMultiPolygon subject = readMultiPoly(reader);
    std::vector<polyclip::PackedMultiPolygon> clips;
    clips.reserve(static_cast<size_t>(numMultiPolys - 1));
    for (int64_t i = 1; i < numMultiPolys; i++) {
      clips.push_back(readMultiPoly(reader));
    }
    if (!reader.done()) throw std::invalid_argument(kBadEncoding);

    return writeMultiPoly(polyclip::clip(opType, subject, clips));
  } catch (const jsi::JSError&) {
    throw;
  } catch (const std::exception& e) {
    // Surface topology/validation failures as JS errors with the original
    // polygon-clipping messages, which callers match on.
    throw jsi::JSError(rt, e.what());
  }
}

std::vector<double> PolygonClippingImpl::splitEach(jsi::Runtime& rt, std::vector<double> subjects,
                                                   std::vector<double> clips) {
  try {
    Reader subjectReader(subjects);
    const polyclip::PackedMultiPolygon subjectPolys = readMultiPoly(subjectReader);
    if (!subjectReader.done()) throw std::invalid_argument(kBadEncoding);
    Reader clipReader(clips);
    const polyclip::PackedMultiPolygon clipPolys = readMultiPoly(clipReader);
    if (!clipReader.done()) throw std::invalid_argument(kBadEncoding);

    const std::vector<polyclip::SplitResult> results = polyclip::splitEach(subjectPolys, clipPolys);

    size_t size = 1;
    for (const auto& r : results) {
      size += 2 + encodedMultiPolySize(r.outside) + encodedMultiPolySize(r.inside);
    }
    std::vector<double> out;
    out.reserve(size);
    out.push_back(static_cast<double>(results.size()));
    for (const auto& r : results) {
      out.push_back(r.touched ? 1.0 : 0.0);
      out.push_back(static_cast<double>(r.failures));
      appendMultiPoly(out, r.outside);
      appendMultiPoly(out, r.inside);
    }
    return out;
  } catch (const jsi::JSError&) {
    throw;
  } catch (const std::exception& e) {
    throw jsi::JSError(rt, e.what());
  }
}

} // namespace facebook::react

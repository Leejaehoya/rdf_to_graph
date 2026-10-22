#ifndef STRING_POOL_HPP
#define STRING_POOL_HPP

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>

using TermId = uint32_t;

// URI / 리터럴 문자열을 한 번만 저장하고 uint32 id 로 참조
class StringPool {
public:
    static constexpr TermId kNoTerm = 0xFFFFFFFFu;

    StringPool() { intern(""); }  // id 0 = 빈 문자열

    TermId intern(std::string_view s) {
        auto it = index_.find(s);
        if (it != index_.end()) return it->second;
        TermId id = static_cast<TermId>(strings_.size());
        strings_.emplace_back(s);
        // deque 는 push_back 해도 기존 원소 주소가 안 바뀌어서 view 키로 써도 안전
        index_.emplace(std::string_view(strings_.back()), id);
        return id;
    }

    TermId find(std::string_view s) const {
        auto it = index_.find(s);
        return it == index_.end() ? kNoTerm : it->second;
    }

    const std::string& str(TermId id) const { return strings_[id]; }
    size_t size() const { return strings_.size(); }

    void clear() {
        index_.clear();
        strings_.clear();
        intern("");
    }

private:
    std::deque<std::string> strings_;
    std::unordered_map<std::string_view, TermId> index_;
};

#endif // STRING_POOL_HPP

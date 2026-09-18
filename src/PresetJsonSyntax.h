#pragma once
#include <cctype>
#include <set>
#include <string>

// Formation의 기존 필드 파서 앞에서 전체 JSON 문법을 검사한다.
// 저장 형식은 중첩 object, string, number뿐이며 배열/불리언은 쓰지 않는다.
namespace presetjson {
class Syntax
{
    const std::string& text;
    std::size_t at = 0;
    void Space() { while (at<text.size() && std::isspace(static_cast<unsigned char>(text[at]))) ++at; }
    bool Take(char c) { Space(); if(at==text.size() || text[at]!=c) return false; ++at; return true; }
    bool String(std::string& value) {
        if(!Take('"')) return false;
        while(at<text.size()) {
            char c=text[at++];
            if(c=='"') return true;
            // 필드명/식별자는 ASCII이며 escape를 생성하지 않는다.
            if(static_cast<unsigned char>(c)<32 || c=='\\') return false;
            value+=c;
        }
        return false;
    }
    bool Number() {
        Space();
        if(at<text.size() && text[at]=='-') ++at;
        if(at==text.size()) return false;
        if(text[at]=='0') ++at;
        else {
            if(text[at]<'1' || text[at]>'9') return false;
            while(at<text.size() && std::isdigit(static_cast<unsigned char>(text[at]))) ++at;
        }
        if(at<text.size() && text[at]=='.') {
            ++at; const auto start=at;
            while(at<text.size() && std::isdigit(static_cast<unsigned char>(text[at]))) ++at;
            if(at==start) return false;
        }
        if(at<text.size() && (text[at]=='e' || text[at]=='E')) {
            ++at; if(at<text.size() && (text[at]=='+' || text[at]=='-')) ++at;
            const auto start=at;
            while(at<text.size() && std::isdigit(static_cast<unsigned char>(text[at]))) ++at;
            if(at==start) return false;
        }
        return true;
    }
    bool Object(unsigned depth) {
        if(depth>16 || !Take('{')) return false;
        std::set<std::string> keys;
        if(Take('}')) return true;
        do {
            std::string key;
            if(!String(key) || !keys.insert(key).second || !Take(':')) return false;
            Space(); if(at==text.size()) return false;
            if(text[at]=='{') { if(!Object(depth+1)) return false; }
            else if(text[at]=='"') { std::string value; if(!String(value)) return false; }
            else if(!Number()) return false;
            if(Take('}')) return true;
        } while(Take(','));
        return false;
    }
public:
    explicit Syntax(const std::string& value) : text(value) {}
    bool Valid() { if(!Object(0)) return false; Space(); return at==text.size(); }
};
inline bool Valid(const std::string& text) { return Syntax(text).Valid(); }
}

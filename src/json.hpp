#pragma once
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace seats {
class Json {
public:
    using Array=std::vector<Json>;
    using Object=std::vector<std::pair<std::string,Json>>;
    Json():value(nullptr){}
    Json(std::nullptr_t):value(nullptr){}
    Json(bool v):value(v){}
    template<class T,std::enable_if_t<std::is_integral_v<T>&&!std::is_same_v<T,bool>,int> =0>
    Json(T v):value(static_cast<int64_t>(v)){}
    Json(const char* v):value(std::string(v)){}
    Json(std::string v):value(std::move(v)){}
    Json(Array v):value(std::move(v)){}
    Json(Object v):value(std::move(v)){}
    static std::string quote(const std::string& text){
        constexpr char hex[]="0123456789abcdef";std::string out="\"";
        for(unsigned char c:text){
            switch(c){
            case '"':out+="\\\"";break;case '\\':out+="\\\\";break;
            case '\b':out+="\\b";break;case '\f':out+="\\f";break;case '\n':out+="\\n";break;
            case '\r':out+="\\r";break;case '\t':out+="\\t";break;
            default:if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}else out+=static_cast<char>(c);
            }
        }
        out+='"';return out;
    }
    std::string dump()const{
        return std::visit([](const auto& v)->std::string{
            using T=std::decay_t<decltype(v)>;
            if constexpr(std::is_same_v<T,std::nullptr_t>)return "null";
            else if constexpr(std::is_same_v<T,bool>)return v?"true":"false";
            else if constexpr(std::is_same_v<T,int64_t>)return std::to_string(v);
            else if constexpr(std::is_same_v<T,std::string>)return quote(v);
            else if constexpr(std::is_same_v<T,Array>){std::string result="[";bool first=true;for(const auto& element:v){if(!first)result+=',';first=false;result+=element.dump();}return result+"]";}
            else{std::string result="{";bool first=true;for(const auto& element:v){if(!first)result+=',';first=false;result+=quote(element.first)+":"+element.second.dump();}return result+"}";}
        },value);
    }
private:
    std::variant<std::nullptr_t,bool,int64_t,std::string,Array,Object> value;
};
}

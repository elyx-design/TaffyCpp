#pragma once
// Test ABI conversions only. No sizing, layout, cache or tree algorithms here.
#include "../../support/tree_suite_oracle.h"
#include <taffy/compute.hpp>
#include <taffy/parse.hpp>
#include <limits>
#include <cmath>
#include <set>
#include <string_view>

namespace cpp_tree_abi {
struct Failure { uint32_t status; std::string message; };
inline void require(bool test, uint32_t status = ORACLE_INVALID, const char* message = "invalid argument") {
    if (!test) throw Failure{status, message};
}
inline bool boolean(uint32_t v) { require(v <= 1, ORACLE_UNSUPPORTED, "invalid boolean"); return v != 0; }
template<class T> inline void put(T* out, T value) { require(out != nullptr); *out = std::move(value); }
template<class T> inline std::span<const T> span(const T* p, size_t n) {
    require((p || !n) && n <= size_t(std::numeric_limits<ptrdiff_t>::max()) / sizeof(T));
    return {p, n};
}
inline std::string_view text(TsText v) {
    span(v.data,v.len);
    // Validate transport UTF-8 just as the reference's str::from_utf8 does.
    for (size_t i=0;i<v.len;) {
        const auto first=static_cast<unsigned char>(v.data[i++]);
        if(first<0x80)continue;
        unsigned remaining; uint32_t cp, minimum;
        if(first>=0xc2 && first<=0xdf){remaining=1;cp=first&31;minimum=0x80;}
        else if(first>=0xe0 && first<=0xef){remaining=2;cp=first&15;minimum=0x800;}
        else if(first>=0xf0 && first<=0xf4){remaining=3;cp=first&7;minimum=0x10000;}
        else throw Failure{ORACLE_INVALID,"invalid UTF-8"};
        require(remaining<=v.len-i,ORACLE_INVALID,"truncated UTF-8");
        while(remaining--){auto next=static_cast<unsigned char>(v.data[i++]);require((next&0xc0)==0x80,ORACLE_INVALID,"invalid UTF-8 continuation");cp=(cp<<6)|(next&63);}
        require(cp>=minimum && cp<=0x10ffff && !(cp>=0xd800 && cp<=0xdfff),ORACLE_INVALID,"invalid UTF-8 codepoint");
    }
    return v.len ? std::string_view(v.data,v.len) : std::string_view{};
}
// ABI-specific JSON representation of native vector<vector<string>>. Production
// parse_css<TemplateLineNames> uses CSS [names], not the reference ABI's JSON.
inline taffy::TemplateLineNames json_line_names(std::string_view source) {
    struct Reader {
        std::string_view text; size_t at=0;
        void ws(){while(at<text.size() && (text[at]==' ' || text[at]=='\t' || text[at]=='\r' || text[at]=='\n'))++at;}
        bool take(char ch){ws();if(at<text.size() && text[at]==ch){++at;return true;}return false;}
        void expect(char ch){require(take(ch),ORACLE_UNSUPPORTED,"invalid line-names JSON");}
        uint32_t hex(){uint32_t cp=0;for(unsigned n=0;n<4;++n){require(at<text.size(),ORACLE_UNSUPPORTED,"truncated JSON escape");auto c=text[at++];unsigned digit=c>='0'&&c<='9'?unsigned(c-'0'):c>='a'&&c<='f'?unsigned(c-'a'+10):c>='A'&&c<='F'?unsigned(c-'A'+10):16;require(digit<16,ORACLE_UNSUPPORTED,"invalid JSON escape");cp=(cp<<4)|digit;}return cp;}
        static void utf8(std::string& result,uint32_t cp){
            if(cp<0x80)result+=char(cp);
            else if(cp<0x800){result+=char(0xc0|(cp>>6));result+=char(0x80|(cp&63));}
            else if(cp<0x10000){result+=char(0xe0|(cp>>12));result+=char(0x80|((cp>>6)&63));result+=char(0x80|(cp&63));}
            else{result+=char(0xf0|(cp>>18));result+=char(0x80|((cp>>12)&63));result+=char(0x80|((cp>>6)&63));result+=char(0x80|(cp&63));}
        }
        std::string string(){
            expect('"');std::string result;
            while(at<text.size()){
                auto c=text[at++];if(c=='"')return result;
                require(static_cast<unsigned char>(c)>=0x20,ORACLE_UNSUPPORTED,"control character in JSON string");
                if(c!='\\'){result+=c;continue;}
                require(at<text.size(),ORACLE_UNSUPPORTED,"truncated JSON escape");
                switch(c=text[at++]){
                case '"':case '\\':case '/':result+=c;break;
                case 'b':result+='\b';break;case 'f':result+='\f';break;case 'n':result+='\n';break;case 'r':result+='\r';break;case 't':result+='\t';break;
                case 'u':{auto cp=hex();if(cp>=0xd800 && cp<=0xdbff){require(at+2<=text.size() && text[at]=='\\' && text[at+1]=='u',ORACLE_UNSUPPORTED,"missing low surrogate");at+=2;auto low=hex();require(low>=0xdc00 && low<=0xdfff,ORACLE_UNSUPPORTED,"invalid low surrogate");cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);}else require(cp<0xdc00 || cp>0xdfff,ORACLE_UNSUPPORTED,"unpaired low surrogate");utf8(result,cp);break;}
                default:throw Failure{ORACLE_UNSUPPORTED,"invalid JSON escape"};
                }
            }
            throw Failure{ORACLE_UNSUPPORTED,"unterminated JSON string"};
        }
    } reader{source};
    taffy::TemplateLineNames result;reader.expect('[');
    if(!reader.take(']')){do{
        reader.expect('[');std::vector<std::string> names;
        if(!reader.take(']')){do{names.push_back(reader.string());}while(reader.take(','));reader.expect(']');}
        result.push_back(std::move(names));
    }while(reader.take(','));reader.expect(']');}
    reader.ws();require(reader.at==source.size(),ORACLE_UNSUPPORTED,"trailing line-names JSON input");return result;
}
inline taffy::Style style(const TsProperty* properties, size_t count) {
    taffy::Style result;
    std::set<std::string_view> seen;
    for (const auto& p : span(properties,count)) {
        auto name = text(p.name), value = text(p.value);
        require(seen.insert(name).second, ORACLE_UNSUPPORTED, "duplicate property");
        // The tree ABI exposes native fields, not production CSS shorthands.
        static const std::set<std::string_view> fields{
            "display","direction","box-sizing","item-is-table","item-is-replaced","item-is-compressible-replaced",
            "overflow-x","overflow-y","scrollbar-width","contain","float","clear","position",
            "width","height","min-width","min-height","max-width","max-height","left","right","top","bottom",
            "margin-left","margin-right","margin-top","margin-bottom","padding-left","padding-right","padding-top","padding-bottom",
            "border-left","border-right","border-top","border-bottom","aspect-ratio","align-items","align-self","justify-items","justify-self",
            "align-content","justify-content","column-gap","row-gap","text-align","flex-direction","flex-wrap","flex-line-count","flex-basis","flex-grow","flex-shrink",
            "grid-template-rows","grid-template-columns","grid-auto-rows","grid-auto-columns","grid-auto-flow",
            "grid-row-start","grid-row-end","grid-column-start","grid-column-end","grid-template-row-names","grid-template-column-names","grid-template-areas"};
        require(fields.contains(name),ORACLE_UNSUPPORTED,"unknown property");
        if(name=="grid-template-row-names"){result.grid_template_row_names=json_line_names(value);continue;}
        if(name=="grid-template-column-names"){result.grid_template_column_names=json_line_names(value);continue;}
        // Native scalar FromStr is case-sensitive and does not trim whitespace.
        if(name=="item-is-table" || name=="item-is-replaced" || name=="item-is-compressible-replaced")
            require(value=="true" || value=="false",ORACLE_UNSUPPORTED,"invalid native boolean");
        if(name=="flex-line-count") {
            auto raw=value; if(!raw.empty() && raw.front()=='+')raw.remove_prefix(1);
            require(!raw.empty() && raw.find_first_not_of("0123456789")==std::string_view::npos,ORACLE_UNSUPPORTED,"invalid native u16");
        }
        if(name=="aspect-ratio" || name=="flex-grow" || name=="flex-shrink" || name=="scrollbar-width") {
            if(name=="aspect-ratio" && value=="auto") {result.aspect_ratio.reset();continue;}
            require(!value.empty() && value.find_first_of(" \t\r\n\f/")==std::string_view::npos,ORACLE_UNSUPPORTED,"invalid native scalar");
            if(name=="aspect-ratio") {
                taffy::Style temporary;
                auto parsed=taffy::parse_style_property(temporary,"flex-grow",value);
                if(!parsed)throw Failure{ORACLE_UNSUPPORTED,parsed.error().message};
                result.aspect_ratio=temporary.flex_grow;continue;
            }
        }
        auto parsed = taffy::parse_style_property(result,name,value);
        if (!parsed) throw Failure{ORACLE_UNSUPPORTED, std::string(name) + ": " + parsed.error().message};
    }
    require(!(seen.contains("grid-template-rows") && seen.contains("grid-template-row-names")) &&
        !(seen.contains("grid-template-columns") && seen.contains("grid-template-column-names")),ORACLE_UNSUPPORTED,"conflicting named-line representations");
    return result;
}
inline std::optional<float> optional(OracleOptional v) { return boolean(v.present) ? std::optional<float>(v.value) : std::nullopt; }
inline OracleOptional optional(std::optional<float> v) { return {uint32_t(v.has_value()),v.value_or(0)}; }
inline taffy::Size<std::optional<float>> known(OracleKnown v) { return {optional(v.width),optional(v.height)}; }
inline OracleKnown known(taffy::Size<std::optional<float>> v) { return {optional(v.width),optional(v.height)}; }
inline taffy::AvailableSpace available(OracleScalar v) {
    switch(v.tag) { case 1:return taffy::AvailableSpace::definite(v.value); case 3:return taffy::AvailableSpace::min_content(); case 4:return taffy::AvailableSpace::max_content(); }
    throw Failure{ORACLE_UNSUPPORTED,"invalid available space tag"};
}
inline OracleScalar available(taffy::AvailableSpace v) { return {v.is_definite()?1u:v.is_min_content()?3u:4u,v.into_option().value_or(0)}; }
inline taffy::Size<taffy::AvailableSpace> available(OracleSizing v) { return {available(v.width),available(v.height)}; }
inline OracleSizing available(taffy::Size<taffy::AvailableSpace> v) { return {available(v.width),available(v.height)}; }
inline taffy::LayoutInput input(const TsInput& v) {
    require(v.run_mode<=2 && v.sizing_mode<=1 && v.axis<=2,ORACLE_UNSUPPORTED,"invalid layout input tag");
    return {static_cast<taffy::RunMode>(v.run_mode),static_cast<taffy::SizingMode>(v.sizing_mode),static_cast<taffy::RequestedAxis>(v.axis),known(v.known_dimensions),
        {boolean(v.definite_width),boolean(v.definite_height)},known(v.parent_size),available(v.available_space),{boolean(v.collapse_start),boolean(v.collapse_end)}};
}
inline TsInput input(const taffy::LayoutInput& v) {
    return {uint32_t(v.run_mode),uint32_t(v.sizing_mode),uint32_t(v.axis),known(v.known_dimensions),uint32_t(v.known_dimensions_are_definite.width),uint32_t(v.known_dimensions_are_definite.height),
        known(v.parent_size),available(v.available_space),uint32_t(v.vertical_margins_are_collapsible.start),uint32_t(v.vertical_margins_are_collapsible.end)};
}
inline OracleSize size(taffy::Size<float> v) { return {v.width,v.height}; }
inline OracleRect rect(taffy::Rect<float> v) { return {v.left,v.right,v.top,v.bottom}; }
inline OracleLayoutOutput output(const taffy::LayoutOutput& v) {
    return {size(v.size),rect(v.scrollable_overflow_rect),optional(v.baselines.first),optional(v.baselines.last),v.top_margin.positive,v.top_margin.negative,v.bottom_margin.positive,v.bottom_margin.negative,
        uint32_t(v.margins_can_collapse_through),uint32_t(v.oof_candidates.size()),uint32_t(v.oof_positioning_area.has_value())};
}
inline taffy::LayoutOutput output(const OracleLayoutOutput& v) {
    require(!v.oof_candidates_count && !v.oof_area_present,ORACLE_UNSUPPORTED,"scalar output cannot encode OOF metadata");
    require(v.top_positive>=0 && v.top_negative<=0 && v.bottom_positive>=0 && v.bottom_negative<=0,
        ORACLE_INVALID,"invalid collapsible margin components");
    taffy::LayoutOutput result;
    result.size={v.size.width,v.size.height}; result.scrollable_overflow_rect={v.scrollable_overflow.left,v.scrollable_overflow.right,v.scrollable_overflow.top,v.scrollable_overflow.bottom};
    result.baselines={optional(v.first_baseline),optional(v.last_baseline)}; result.top_margin={v.top_positive,v.top_negative}; result.bottom_margin={v.bottom_positive,v.bottom_negative};
    result.margins_can_collapse_through=boolean(v.margins_can_collapse_through); return result;
}
inline TsLayout layout(const taffy::Layout& v) { return {v.order,v.location.x,v.location.y,size(v.size),size(v.content_box_size()),size(v.scrollbar_size),rect(v.scrollable_overflow_rect),rect(v.border),rect(v.padding),rect(v.margin),v.scroll_width(),v.scroll_height()}; }
inline void string_out(std::string_view value,char* out,size_t capacity,size_t* required) {
    put(required,value.size()+1);
    if (!out && !capacity) return;
    require(out && capacity>value.size(),ORACLE_INVALID,"short string output");
    std::copy(value.begin(),value.end(),out); out[value.size()]=0;
}
} // namespace cpp_tree_abi

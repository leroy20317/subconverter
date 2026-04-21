#include <set>
#include <string>

#include "handler/settings.h"
#include "utils/logger.h"
#include "utils/network.h"
#include "utils/regexp.h"
#include "utils/string_hash.h"
#include "utils/string.h"
#include "utils/rapidjson_extra.h"
#include "subexport.h"

/// rule type lists
#define basic_types "DOMAIN", "DOMAIN-SUFFIX", "DOMAIN-KEYWORD", "IP-CIDR", "SRC-IP-CIDR", "GEOIP", "MATCH", "FINAL"
// 新增meta路由规则
//string_array ClashRuleTypes = {basic_types, "IP-CIDR6", "SRC-PORT", "DST-PORT", "PROCESS-NAME"};
string_array ClashRuleTypes = {basic_types, "IP-CIDR6", "SRC-PORT", "DST-PORT", "PROCESS-NAME", "DOMAIN-REGEX", "GEOSITE", "IP-SUFFIX", "IP-ASN", "SRC-GEOIP", "SRC-IP-ASN", "SRC-IP-SUFFIX", "IN-PORT", "IN-TYPE", "IN-USER", "IN-NAME", "PROCESS-PATH-REGEX", "PROCESS-PATH", "PROCESS-NAME-REGEX", "UID", "NETWORK", "DSCP", "SUB-RULE", "RULE-SET", "AND", "OR", "NOT"};
string_array Surge2RuleTypes = {basic_types, "IP-CIDR6", "USER-AGENT", "URL-REGEX", "PROCESS-NAME", "IN-PORT", "DEST-PORT", "SRC-IP"};
string_array SurgeRuleTypes = {basic_types, "IP-CIDR6", "USER-AGENT", "URL-REGEX", "AND", "OR", "NOT", "PROCESS-NAME", "IN-PORT", "DEST-PORT", "SRC-IP"};
string_array QuanXRuleTypes = {basic_types, "USER-AGENT", "HOST", "HOST-SUFFIX", "HOST-KEYWORD"};
string_array SurfRuleTypes = {basic_types, "IP-CIDR6", "PROCESS-NAME", "IN-PORT", "DEST-PORT", "SRC-IP"};
string_array SingBoxRuleTypes = {basic_types, "IP-VERSION", "INBOUND", "PROTOCOL", "NETWORK", "GEOSITE", "SRC-GEOIP", "DOMAIN-REGEX", "PROCESS-NAME", "PROCESS-PATH", "PACKAGE-NAME", "PORT", "PORT-RANGE", "SRC-PORT", "SRC-PORT-RANGE", "USER", "USER-ID"};

std::string convertRuleset(const std::string &content, int type)
{
    /// Target: Surge type,pattern[,flag]
    /// Source: QuanX type,pattern[,group]
    ///         Clash payload:\n  - 'ipcidr/domain/classic(Surge-like)'

    std::string output, strLine;

    if(type == RULESET_SURGE)
        return content;

    if(regFind(content, "^payload:\\r?\\n")) /// Clash
    {
        output = regReplace(regReplace(content, "payload:\\r?\\n", "", true), R"(\s?^\s*-\s+('|"?)(.*)\1$)", "\n$2", true);
        if(type == RULESET_CLASH_CLASSICAL) /// classical type
            return output;
        std::stringstream ss;
        ss << output;
        char delimiter = getLineBreak(output);
        output.clear();
        string_size pos, lineSize;
        while(getline(ss, strLine, delimiter))
        {
            strLine = trim(strLine);
            lineSize = strLine.size();
            if(lineSize && strLine[lineSize - 1] == '\r') //remove line break
                strLine.erase(--lineSize);

            if(strFind(strLine, "//"))
            {
                strLine.erase(strLine.find("//"));
                strLine = trimWhitespace(strLine);
            }

            if(!strLine.empty() && (strLine[0] != ';' && strLine[0] != '#' && !(lineSize >= 2 && strLine[0] == '/' && strLine[1] == '/')))
            {
                pos = strLine.find('/');
                if(pos != std::string::npos) /// ipcidr
                {
                    if(isIPv4(strLine.substr(0, pos)))
                        output += "IP-CIDR,";
                    else
                        output += "IP-CIDR6,";
                }
                else
                {
                    if(strLine[0] == '.' || (lineSize >= 2 && strLine[0] == '+' && strLine[1] == '.')) /// suffix
                    {
                        bool keyword_flag = false;
                        while(endsWith(strLine, ".*"))
                        {
                            keyword_flag = true;
                            strLine.erase(strLine.size() - 2);
                        }
                        output += "DOMAIN-";
                        if(keyword_flag)
                            output += "KEYWORD,";
                        else
                            output += "SUFFIX,";
                        strLine.erase(0, 2 - (strLine[0] == '.'));
                    }
                    else
                        output += "DOMAIN,";
                }
            }
            output += strLine;
            output += '\n';
        }
        return output;
    }
    else /// QuanX
    {
        output = regReplace(regReplace(content, "^(?i:host)", "DOMAIN", true), "^(?i:ip6-cidr)", "IP-CIDR6", true); //translate type
        output = regReplace(output, "^((?i:DOMAIN(?:-(?:SUFFIX|KEYWORD))?|IP-CIDR6?|USER-AGENT),)\\s*?(\\S*?)(?:,(?!no-resolve).*?)(,no-resolve)?$", "\\U$1\\E$2${3:-}", true); //remove group
        return output;
    }
}

static std::string transformRuleToCommon(string_view_array &temp, const std::string &input, const std::string &group, bool no_resolve_only = false)
{
    temp.clear();
    std::string strLine;
    split(temp, input, ',');
    if(temp.size() < 2)
    {
        strLine = trimWhitespace(std::string(temp[0]), true, true);
        strLine += ",";
        strLine += group;
    }
    else
    {
        const std::string rule_type = trimWhitespace(std::string(temp[0]), true, true);
        const std::string rule_value = trimWhitespace(std::string(temp[1]), true, true);

        strLine = rule_type;
        strLine += ",";
        strLine += rule_value;
        strLine += ",";
        strLine += group;
        if(temp.size() > 2)
        {
            const std::string rule_option = trimWhitespace(std::string(temp[2]), true, true);
            if(!rule_option.empty() && (!no_resolve_only || rule_option == "no-resolve"))
            {
                strLine += ",";
                strLine += rule_option;
            }
        }
    }
    return strLine;
}

void rulesetToClash(YAML::Node &base_rule, std::vector<RulesetContent> &ruleset_content_array, bool overwrite_original_rules, bool new_field_name)
{
    string_array allRules;
    std::string rule_group, retrieved_rules, strLine;
    std::stringstream strStrm;
    const std::string field_name = new_field_name ? "rules" : "Rule";
    YAML::Node rules;
    size_t total_rules = 0;

    if(!overwrite_original_rules && base_rule[field_name].IsDefined())
        rules = base_rule[field_name];

    std::vector<std::string_view> temp(4);
    for(RulesetContent &x : ruleset_content_array)
    {
        if(global.maxAllowedRules && total_rules > global.maxAllowedRules)
            break;
        rule_group = x.rule_group;
        retrieved_rules = x.rule_content.get();
        if(retrieved_rules.empty())
        {
            writeLog(0, "Failed to fetch ruleset or ruleset is empty: '" + x.rule_path + "'!", LOG_LEVEL_WARNING);
            continue;
        }
        if(startsWith(retrieved_rules, "[]"))
        {
            strLine = retrieved_rules.substr(2);
            if(startsWith(strLine, "FINAL"))
                strLine.replace(0, 5, "MATCH");
            strLine = transformRuleToCommon(temp, strLine, rule_group);
            allRules.emplace_back(strLine);
            total_rules++;
            continue;
        }
        retrieved_rules = convertRuleset(retrieved_rules, x.rule_type);
        char delimiter = getLineBreak(retrieved_rules);

        strStrm.clear();
        strStrm<<retrieved_rules;
        std::string::size_type lineSize;
        while(getline(strStrm, strLine, delimiter))
        {
            if(global.maxAllowedRules && total_rules > global.maxAllowedRules)
                break;
            strLine = trimWhitespace(strLine, true, true); //remove whitespaces
            lineSize = strLine.size();
            if(!lineSize || strLine[0] == ';' || strLine[0] == '#' || (lineSize >= 2 && strLine[0] == '/' && strLine[1] == '/')) //empty lines and comments are ignored
                continue;
            if(std::none_of(ClashRuleTypes.begin(), ClashRuleTypes.end(), [strLine](const std::string& type){return startsWith(strLine, type);}))
                continue;
            if(strFind(strLine, "//"))
            {
                strLine.erase(strLine.find("//"));
                strLine = trimWhitespace(strLine);
            }
            strLine = transformRuleToCommon(temp, strLine, rule_group);
            allRules.emplace_back(strLine);
        }
    }

    for(std::string &x : allRules)
    {
        rules.push_back(x);
    }

    base_rule[field_name] = rules;
}

std::string rulesetToClashStr(YAML::Node &base_rule, std::vector<RulesetContent> &ruleset_content_array, bool overwrite_original_rules, bool new_field_name)
{
    std::string rule_group, retrieved_rules, strLine;
    std::stringstream strStrm;
    const std::string field_name = new_field_name ? "rules" : "Rule";
    std::string output_content = "\n" + field_name + ":\n";
    size_t total_rules = 0;

    if(!overwrite_original_rules && base_rule[field_name].IsDefined())
    {
        for(size_t i = 0; i < base_rule[field_name].size(); i++)
            output_content += "  - " + safe_as<std::string>(base_rule[field_name][i]) + "\n";
    }
    base_rule.remove(field_name);

    string_view_array temp(4);
    for(RulesetContent &x : ruleset_content_array)
    {
        if(global.maxAllowedRules && total_rules > global.maxAllowedRules)
            break;
        rule_group = x.rule_group;
        retrieved_rules = x.rule_content.get();
        if(retrieved_rules.empty())
        {
            writeLog(0, "Failed to fetch ruleset or ruleset is empty: '" + x.rule_path + "'!", LOG_LEVEL_WARNING);
            continue;
        }
        if(startsWith(retrieved_rules, "[]"))
        {
            strLine = retrieved_rules.substr(2);
            if(startsWith(strLine, "FINAL"))
                strLine.replace(0, 5, "MATCH");
            strLine = transformRuleToCommon(temp, strLine, rule_group);
            output_content += "  - " + strLine + "\n";
            total_rules++;
            continue;
        }
        retrieved_rules = convertRuleset(retrieved_rules, x.rule_type);
        char delimiter = getLineBreak(retrieved_rules);

        strStrm.clear();
        strStrm<<retrieved_rules;
        std::string::size_type lineSize;
        while(getline(strStrm, strLine, delimiter))
        {
            if(global.maxAllowedRules && total_rules > global.maxAllowedRules)
                break;
            strLine = trimWhitespace(strLine, true, true); //remove whitespaces
            lineSize = strLine.size();
            if(!lineSize || strLine[0] == ';' || strLine[0] == '#' || (lineSize >= 2 && strLine[0] == '/' && strLine[1] == '/')) //empty lines and comments are ignored
                continue;
            if(std::none_of(ClashRuleTypes.begin(), ClashRuleTypes.end(), [strLine](const std::string& type){ return startsWith(strLine, type); }))
                continue;
            if(strFind(strLine, "//"))
            {
                strLine.erase(strLine.find("//"));
                strLine = trimWhitespace(strLine);
            }

            //AND & OR & NOT
            if(startsWith(strLine, "AND") || startsWith(strLine, "OR") || startsWith(strLine, "NOT"))
            {
                output_content += "  - " + strLine + "," + rule_group + "\n";
            }
            //SUB-RULE & RULE-SET
            else if (startsWith(strLine, "SUB-RULE") || startsWith(strLine, "RULE-SET"))
            {
                output_content += "  - " + strLine + "\n";
            }
            else
            //OTHER
            {
                strLine = transformRuleToCommon(temp, strLine, rule_group);
                output_content += "  - " + strLine + "\n";
            }

            //strLine = transformRuleToCommon(temp, strLine, rule_group);
            //output_content += "  - " + strLine + "\n";
            total_rules++;
        }
    }
    return output_content;
}

void rulesetToSurge(INIReader &base_rule, std::vector<RulesetContent> &ruleset_content_array, int surge_ver, bool overwrite_original_rules, const std::string &remote_path_prefix)
{
    string_array allRules;
    std::string rule_group, rule_path, rule_path_typed, retrieved_rules, strLine;
    std::stringstream strStrm;
    size_t total_rules = 0;

    switch(surge_ver) //other version: -3 for Surfboard, -4 for Loon
    {
    case 0:
        base_rule.set_current_section("RoutingRule"); //Mellow
        break;
    case -1:
        base_rule.set_current_section("filter_local"); //Quantumult X
        break;
    case -2:
        base_rule.set_current_section("TCP"); //Quantumult
        break;
    default:
        base_rule.set_current_section("Rule");
    }

    if(overwrite_original_rules)
    {
        base_rule.erase_section();
        switch(surge_ver)
        {
        case -1:
            base_rule.erase_section("filter_remote");
            break;
        case -4:
            base_rule.erase_section("Remote Rule");
            break;
        default:
            break;
        }
    }

    const std::string rule_match_regex = "^(.*?,.*?)(,.*)(,.*)$";

    string_view_array temp(4);
    for(RulesetContent &x : ruleset_content_array)
    {
        if(global.maxAllowedRules && total_rules > global.maxAllowedRules)
            break;
        rule_group = x.rule_group;
        rule_path = x.rule_path;
        rule_path_typed = x.rule_path_typed;
        if(rule_path.empty())
        {
            strLine = x.rule_content.get().substr(2);
            if(strLine == "MATCH")
                strLine = "FINAL";
            if(surge_ver == -1 || surge_ver == -2)
            {
                strLine = transformRuleToCommon(temp, strLine, rule_group, true);
            }
            else
            {
                if(!startsWith(strLine, "AND") && !startsWith(strLine, "OR") && !startsWith(strLine, "NOT"))
                    strLine = transformRuleToCommon(temp, strLine, rule_group);
            }
            strLine = replaceAllDistinct(strLine, ",,", ",");
            allRules.emplace_back(strLine);
            total_rules++;
            continue;
        }
        else
        {
            if(surge_ver == -1 && x.rule_type == RULESET_QUANX && isLink(rule_path))
            {
                strLine = rule_path + ", tag=" + rule_group + ", force-policy=" + rule_group + ", enabled=true";
                base_rule.set("filter_remote", "{NONAME}", strLine);
                continue;
            }
            if(fileExist(rule_path))
            {
                if(surge_ver > 2 && !remote_path_prefix.empty())
                {
                    strLine = "RULE-SET," + remote_path_prefix + "/getruleset?type=1&url=" + urlSafeBase64Encode(rule_path_typed) + "," + rule_group;
                    if(x.update_interval)
                        strLine += ",update-interval=" + std::to_string(x.update_interval);
                    allRules.emplace_back(strLine);
                    continue;
                }
                else if(surge_ver == -1 && !remote_path_prefix.empty())
                {
                    strLine = remote_path_prefix + "/getruleset?type=2&url=" + urlSafeBase64Encode(rule_path_typed) + "&group=" + urlSafeBase64Encode(rule_group);
                    strLine += ", tag=" + rule_group + ", enabled=true";
                    base_rule.set("filter_remote", "{NONAME}", strLine);
                    continue;
                }
                else if(surge_ver == -4 && !remote_path_prefix.empty())
                {
                    strLine = remote_path_prefix + "/getruleset?type=1&url=" + urlSafeBase64Encode(rule_path_typed) + "," + rule_group;
                    base_rule.set("Remote Rule", "{NONAME}", strLine);
                    continue;
                }
            }
            else if(isLink(rule_path))
            {
                if(surge_ver > 2)
                {
                    if(x.rule_type != RULESET_SURGE)
                    {
                        if(!remote_path_prefix.empty())
                            strLine = "RULE-SET," + remote_path_prefix + "/getruleset?type=1&url=" + urlSafeBase64Encode(rule_path_typed) + "," + rule_group;
                        else
                            continue;
                    }
                    else
                        strLine = "RULE-SET," + rule_path + "," + rule_group;

                    if(x.update_interval)
                        strLine += ",update-interval=" + std::to_string(x.update_interval);

                    allRules.emplace_back(strLine);
                    continue;
                }
                else if(surge_ver == -1 && !remote_path_prefix.empty())
                {
                    strLine = remote_path_prefix + "/getruleset?type=2&url=" + urlSafeBase64Encode(rule_path_typed) + "&group=" + urlSafeBase64Encode(rule_group);
                    strLine += ", tag=" + rule_group + ", enabled=true";
                    base_rule.set("filter_remote", "{NONAME}", strLine);
                    continue;
                }
                else if(surge_ver == -4)
                {
                    strLine = rule_path + "," + rule_group;
                    base_rule.set("Remote Rule", "{NONAME}", strLine);
                    continue;
                }
            }
            else
                continue;
            retrieved_rules = x.rule_content.get();
            if(retrieved_rules.empty())
            {
                writeLog(0, "Failed to fetch ruleset or ruleset is empty: '" + x.rule_path + "'!", LOG_LEVEL_WARNING);
                continue;
            }

            retrieved_rules = convertRuleset(retrieved_rules, x.rule_type);
            char delimiter = getLineBreak(retrieved_rules);

            strStrm.clear();
            strStrm<<retrieved_rules;
            std::string::size_type lineSize;
            while(getline(strStrm, strLine, delimiter))
            {
                if(global.maxAllowedRules && total_rules > global.maxAllowedRules)
                    break;
                strLine = trimWhitespace(strLine, true, true);
                lineSize = strLine.size();
                if(!lineSize || strLine[0] == ';' || strLine[0] == '#' || (lineSize >= 2 && strLine[0] == '/' && strLine[1] == '/')) //empty lines and comments are ignored
                    continue;

                /// remove unsupported types
                switch(surge_ver)
                {
                case -2:
                    if(startsWith(strLine, "IP-CIDR6"))
                        continue;
                    [[fallthrough]];
                case -1:
                    if(!std::any_of(QuanXRuleTypes.begin(), QuanXRuleTypes.end(), [strLine](const std::string& type){return startsWith(strLine, type);}))
                        continue;
                    break;
                case -3:
                    if(!std::any_of(SurfRuleTypes.begin(), SurfRuleTypes.end(), [strLine](const std::string& type){return startsWith(strLine, type);}))
                        continue;
                    break;
                default:
                    if(surge_ver > 2)
                    {
                        if(!std::any_of(SurgeRuleTypes.begin(), SurgeRuleTypes.end(), [strLine](const std::string& type){return startsWith(strLine, type);}))
                            continue;
                    }
                    else
                    {
                        if(!std::any_of(Surge2RuleTypes.begin(), Surge2RuleTypes.end(), [strLine](const std::string& type){return startsWith(strLine, type);}))
                            continue;
                    }
                }

                if(strFind(strLine, "//"))
                {
                    strLine.erase(strLine.find("//"));
                    strLine = trimWhitespace(strLine);
                }

                if(surge_ver == -1 || surge_ver == -2)
                {
                    if(startsWith(strLine, "IP-CIDR6"))
                        strLine.replace(0, 8, "IP6-CIDR");
                    strLine = transformRuleToCommon(temp, strLine, rule_group, true);
                }
                else
                {
                    if(!startsWith(strLine, "AND") && !startsWith(strLine, "OR") && !startsWith(strLine, "NOT"))
                        strLine = transformRuleToCommon(temp, strLine, rule_group);
                }
                allRules.emplace_back(strLine);
                total_rules++;
            }
        }
    }

    for(std::string &x : allRules)
    {
        base_rule.set("{NONAME}", x);
    }
}

enum class SingBoxRuleParseResult
{
    Unsupported,
    Matcher,
    RouteRule,
    Final
};

static std::string normalizeSingBoxRuleType(const std::string &type)
{
    auto normalized = toLower(trimWhitespace(type));
    normalized = replaceAllDistinct(normalized, "-", "_");
    normalized = replaceAllDistinct(normalized, "ip_cidr6", "ip_cidr");
    normalized = replaceAllDistinct(normalized, "src_", "source_");
    return normalized;
}

static void addSingBoxStringArrayMember(rapidjson::Value &rule, const char *name, const std::string &value,
                                        rapidjson::MemoryPoolAllocator<> &allocator)
{
    rapidjson::Value values(rapidjson::kArrayType);
    values.PushBack(rapidjson::Value(value.c_str(), allocator), allocator);
    rule.AddMember(rapidjson::Value(name, allocator), values, allocator);
}

static bool addSingBoxIntArrayMember(rapidjson::Value &rule, const char *name, const std::string &value,
                                     rapidjson::MemoryPoolAllocator<> &allocator)
{
    if (!regMatch(value, R"(^\d+$)"))
        return false;

    rapidjson::Value values(rapidjson::kArrayType);
    values.PushBack(to_int(value), allocator);
    rule.AddMember(rapidjson::Value(name, allocator), values, allocator);
    return true;
}

static rapidjson::Value buildSingBoxRoutedRule(rapidjson::Value rule, const std::string &group,
                                               rapidjson::MemoryPoolAllocator<> &allocator)
{
    rule.AddMember("action", "route", allocator);
    rule.AddMember("outbound", rapidjson::Value(group.c_str(), allocator), allocator);
    return rule;
}

static rapidjson::Value buildSingBoxRuleSetReference(const std::string &tag, const std::string &group,
                                                     rapidjson::MemoryPoolAllocator<> &allocator,
                                                     bool match_source = false)
{
    rapidjson::Value rule(rapidjson::kObjectType);
    rapidjson::Value rule_sets(rapidjson::kArrayType);
    rule_sets.PushBack(rapidjson::Value(tag.c_str(), allocator), allocator);
    rule.AddMember("rule_set", rule_sets, allocator);
    if (match_source)
        rule.AddMember("rule_set_ipcidr_match_source", true, allocator);
    return buildSingBoxRoutedRule(std::move(rule), group, allocator);
}

static rapidjson::Value buildSingBoxOfficialRuleSet(const std::string &tag, rapidjson::MemoryPoolAllocator<> &allocator)
{
    const bool is_geosite = startsWith(tag, "geosite-");
    const std::string repo = is_geosite ? "sing-geosite" : "sing-geoip";
    const std::string url = "https://raw.githubusercontent.com/SagerNet/" + repo + "/rule-set/" + tag + ".srs";

    rapidjson::Value rule_set(rapidjson::kObjectType);
    rule_set.AddMember("type", "remote", allocator);
    rule_set.AddMember("tag", rapidjson::Value(tag.c_str(), allocator), allocator);
    rule_set.AddMember("format", "binary", allocator);
    rule_set.AddMember("url", rapidjson::Value(url.c_str(), allocator), allocator);
    rule_set.AddMember("download_detour", "DIRECT", allocator);
    return rule_set;
}

static std::string makeSingBoxInlineRuleSetTag(const RulesetContent &ruleset, std::size_t index)
{
    std::string seed = ruleset.rule_path_typed.empty() ? ruleset.rule_path : ruleset.rule_path_typed;
    if (seed.empty())
        seed = ruleset.rule_group + "-" + std::to_string(index);
    return "subconverter-" + std::to_string(hash_(seed));
}

static SingBoxRuleParseResult parseSingBoxRule(std::vector<std::string_view> &args, const std::string &rule,
                                               rapidjson::Value &out_rule, std::string &final_outbound,
                                               std::string &required_rule_set_tag,
                                               bool &rule_set_match_source,
                                               rapidjson::MemoryPoolAllocator<> &allocator)
{
    args.clear();
    split(args, rule, ',');
    if (args.size() < 2)
        return SingBoxRuleParseResult::Unsupported;

    const std::string normalized_type = normalizeSingBoxRuleType(std::string(args[0]));
    const std::string raw_value = trimWhitespace(std::string(args[1]));
    const std::string normalized_value = toLower(raw_value);

    out_rule.SetObject();
    final_outbound.clear();
    required_rule_set_tag.clear();
    rule_set_match_source = false;

    if (normalized_type == "match" || normalized_type == "final")
    {
        final_outbound = raw_value;
        return SingBoxRuleParseResult::Final;
    }

    if (normalized_type == "geoip")
    {
        if (normalized_value == "private")
            out_rule.AddMember("ip_is_private", true, allocator);
        else
        {
            required_rule_set_tag = "geoip-" + normalized_value;
            return SingBoxRuleParseResult::RouteRule;
        }
        return SingBoxRuleParseResult::RouteRule;
    }

    if (normalized_type == "source_geoip")
    {
        if (normalized_value == "private")
            out_rule.AddMember("source_ip_is_private", true, allocator);
        else
        {
            required_rule_set_tag = "geoip-" + normalized_value;
            rule_set_match_source = true;
            return SingBoxRuleParseResult::RouteRule;
        }
        return SingBoxRuleParseResult::RouteRule;
    }

    if (normalized_type == "geosite")
    {
        required_rule_set_tag = "geosite-" + normalized_value;
        return SingBoxRuleParseResult::RouteRule;
    }

    if (normalized_type == "ip_version")
    {
        if (!regMatch(raw_value, R"(^\d+$)"))
            return SingBoxRuleParseResult::Unsupported;
        out_rule.AddMember("ip_version", to_int(raw_value), allocator);
        return SingBoxRuleParseResult::Matcher;
    }

    if (normalized_type == "port")
    {
        if (!addSingBoxIntArrayMember(out_rule, "port", raw_value, allocator))
            return SingBoxRuleParseResult::Unsupported;
        return SingBoxRuleParseResult::Matcher;
    }

    if (normalized_type == "source_port")
    {
        if (!addSingBoxIntArrayMember(out_rule, "source_port", raw_value, allocator))
            return SingBoxRuleParseResult::Unsupported;
        return SingBoxRuleParseResult::Matcher;
    }

    if (normalized_type == "user_id")
    {
        if (!addSingBoxIntArrayMember(out_rule, "user_id", raw_value, allocator))
            return SingBoxRuleParseResult::Unsupported;
        return SingBoxRuleParseResult::Matcher;
    }

    if (normalized_type == "protocol" || normalized_type == "network")
    {
        out_rule.AddMember(rapidjson::Value(normalized_type.c_str(), allocator),
                           rapidjson::Value(raw_value.c_str(), allocator), allocator);
        return SingBoxRuleParseResult::Matcher;
    }

    if (normalized_type == "port_range" || normalized_type == "source_port_range" || normalized_type == "domain" ||
        normalized_type == "domain_suffix" || normalized_type == "domain_keyword" || normalized_type == "domain_regex" ||
        normalized_type == "ip_cidr" || normalized_type == "source_ip_cidr" || normalized_type == "inbound" ||
        normalized_type == "process_name" || normalized_type == "process_path" || normalized_type == "package_name" ||
        normalized_type == "user")
    {
        addSingBoxStringArrayMember(out_rule, normalized_type.c_str(), raw_value, allocator);
        return SingBoxRuleParseResult::Matcher;
    }

    return SingBoxRuleParseResult::Unsupported;
}

void rulesetToSingBox(rapidjson::Document &base_rule, std::vector<RulesetContent> &ruleset_content_array, bool overwrite_original_rules)
{
    using namespace rapidjson_ext;
    (void)overwrite_original_rules;
    std::string rule_group, retrieved_rules, strLine, final;
    std::stringstream strStrm;
    size_t total_rules = 0;
    auto &allocator = base_rule.GetAllocator();

    rapidjson::Value rules(rapidjson::kArrayType);
    rapidjson::Value rule_sets(rapidjson::kArrayType);
    std::set<std::string> existing_rule_set_tags;
    if (base_rule.HasMember("route") && base_rule["route"].IsObject())
    {
        if (base_rule["route"].HasMember("rules") && base_rule["route"]["rules"].IsArray())
            rules.Swap(base_rule["route"]["rules"]);
        if (base_rule["route"].HasMember("rule_set") && base_rule["route"]["rule_set"].IsArray())
        {
            for (const auto &rule_set : base_rule["route"]["rule_set"].GetArray())
            {
                if (rule_set.IsObject() && rule_set.HasMember("tag") && rule_set["tag"].IsString())
                    existing_rule_set_tags.insert(rule_set["tag"].GetString());
            }
            rule_sets.Swap(base_rule["route"]["rule_set"]);
        }
        if (base_rule["route"].HasMember("final") && base_rule["route"]["final"].IsString())
            final = base_rule["route"]["final"].GetString();
    }

    if (global.singBoxAddClashModes)
    {
        auto global_object = buildObject(allocator, "clash_mode", "Global", "action", "route", "outbound", "GLOBAL");
        auto direct_object = buildObject(allocator, "clash_mode", "Direct", "action", "route", "outbound", "DIRECT");
        rules.PushBack(global_object, allocator);
        rules.PushBack(direct_object, allocator);
    }

    std::vector<std::string_view> temp(4);
    for(std::size_t i = 0; i < ruleset_content_array.size(); i++)
    {
        RulesetContent &x = ruleset_content_array[i];
        if(global.maxAllowedRules && total_rules > global.maxAllowedRules)
            break;
        rule_group = x.rule_group;
        retrieved_rules = x.rule_content.get();
        if(retrieved_rules.empty())
        {
            writeLog(0, "Failed to fetch ruleset or ruleset is empty: '" + x.rule_path + "'!", LOG_LEVEL_WARNING);
            continue;
        }
        if(startsWith(retrieved_rules, "[]"))
        {
            strLine = retrieved_rules.substr(2);
            rapidjson::Value parsed_rule(rapidjson::kObjectType);
            std::string parsed_final, required_rule_set_tag;
            bool rule_set_match_source = false;
            switch(parseSingBoxRule(temp, strLine, parsed_rule, parsed_final, required_rule_set_tag,
                                    rule_set_match_source, allocator))
            {
            case SingBoxRuleParseResult::Final:
                final = rule_group;
                break;
            case SingBoxRuleParseResult::RouteRule:
                if(!required_rule_set_tag.empty())
                {
                    if(existing_rule_set_tags.insert(required_rule_set_tag).second)
                        rule_sets.PushBack(buildSingBoxOfficialRuleSet(required_rule_set_tag, allocator), allocator);
                    rules.PushBack(buildSingBoxRuleSetReference(required_rule_set_tag, rule_group, allocator, rule_set_match_source), allocator);
                }
                else
                    rules.PushBack(buildSingBoxRoutedRule(std::move(parsed_rule), rule_group, allocator), allocator);
                total_rules++;
                break;
            case SingBoxRuleParseResult::Matcher:
                rules.PushBack(buildSingBoxRoutedRule(std::move(parsed_rule), rule_group, allocator), allocator);
                total_rules++;
                break;
            default:
                break;
            }
            continue;
        }
        retrieved_rules = convertRuleset(retrieved_rules, x.rule_type);
        char delimiter = getLineBreak(retrieved_rules);

        strStrm.str("");
        strStrm.clear();
        strStrm<<retrieved_rules;

        std::string::size_type lineSize;
        rapidjson::Value inline_rules(rapidjson::kArrayType);
        const std::string inline_rule_set_tag = makeSingBoxInlineRuleSetTag(x, i);

        while(getline(strStrm, strLine, delimiter))
        {
            if(global.maxAllowedRules && total_rules > global.maxAllowedRules)
                break;
            strLine = trimWhitespace(strLine, true, true); //remove whitespaces
            lineSize = strLine.size();
            if(!lineSize || strLine[0] == ';' || strLine[0] == '#' || (lineSize >= 2 && strLine[0] == '/' && strLine[1] == '/')) //empty lines and comments are ignored
                continue;
            if(strFind(strLine, "//"))
            {
                strLine.erase(strLine.find("//"));
                strLine = trimWhitespace(strLine);
            }
            rapidjson::Value parsed_rule(rapidjson::kObjectType);
            std::string parsed_final, required_rule_set_tag;
            bool rule_set_match_source = false;
            switch(parseSingBoxRule(temp, strLine, parsed_rule, parsed_final, required_rule_set_tag,
                                    rule_set_match_source, allocator))
            {
            case SingBoxRuleParseResult::Final:
                final = rule_group;
                break;
            case SingBoxRuleParseResult::RouteRule:
                if(!required_rule_set_tag.empty())
                {
                    if(existing_rule_set_tags.insert(required_rule_set_tag).second)
                        rule_sets.PushBack(buildSingBoxOfficialRuleSet(required_rule_set_tag, allocator), allocator);
                    rules.PushBack(buildSingBoxRuleSetReference(required_rule_set_tag, rule_group, allocator, rule_set_match_source), allocator);
                }
                else
                    rules.PushBack(buildSingBoxRoutedRule(std::move(parsed_rule), rule_group, allocator), allocator);
                total_rules++;
                break;
            case SingBoxRuleParseResult::Matcher:
                inline_rules.PushBack(parsed_rule, allocator);
                total_rules++;
                break;
            default:
                break;
            }
        }
        if (!inline_rules.Empty())
        {
            if (existing_rule_set_tags.insert(inline_rule_set_tag).second)
            {
                rapidjson::Value rule_set(rapidjson::kObjectType);
                rule_set.AddMember("type", "inline", allocator);
                rule_set.AddMember("tag", rapidjson::Value(inline_rule_set_tag.c_str(), allocator), allocator);
                rule_set.AddMember("rules", inline_rules, allocator);
                rule_sets.PushBack(rule_set, allocator);
            }
            rules.PushBack(buildSingBoxRuleSetReference(inline_rule_set_tag, rule_group, allocator), allocator);
        }
    }

    if (!base_rule.HasMember("route"))
        base_rule.AddMember("route", rapidjson::Value(rapidjson::kObjectType), allocator);

    base_rule["route"]
    | AddMemberOrReplace("rules", rules, allocator)
    | AddMemberOrReplace("rule_set", rule_sets, allocator);

    if (!final.empty())
    {
        auto finalValue = rapidjson::Value(final.c_str(), allocator);
        base_rule["route"] | AddMemberOrReplace("final", finalValue, allocator);
    }
}

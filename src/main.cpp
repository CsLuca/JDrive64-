#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <cstdint>
#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "jdrive64/d64_image_editor.hpp"
#include "jdrive64/disk_image_session.hpp"
#include "jdrive64/mount_backend.hpp"
#include "jdrive64/winfsp_filesystem.hpp"
#include "jdrive64/winfsp_runtime.hpp"

namespace {

using jdrive64::DiskImageSession;
using jdrive64::WinFspFilesystem;
using jdrive64::D64ImageEditor;
using jdrive64::WinFspRuntime;

constexpr std::uint32_t kD64BlockSizeBytes = 256;
constexpr std::uint32_t kD64TotalBlocks = 664;

std::filesystem::path MountStateRoot() {
  return std::filesystem::temp_directory_path() / "jdrive64_mounts";
}

std::string NormalizeMountPoint(std::string mount_point) {
  for (char& c : mount_point) {
    if (c >= 'a' && c <= 'z') {
      c = static_cast<char>(c - 'a' + 'A');
    }
  }
  return mount_point;
}

bool IsValidMountPoint(const std::string& mount_point) {
  if (mount_point.size() != 2 || mount_point[1] != ':') {
    return false;
  }
  return mount_point[0] >= 'A' && mount_point[0] <= 'Z';
}

std::filesystem::path MountStateFile(const std::string& mount_point) {
  const std::string file_name(1, mount_point[0]);
  return MountStateRoot() / (file_name + ".state");
}

struct MountState {
  std::string mount_point;
  std::string image_path;
  std::string backend_name;
  std::string telemetry_jsonl_path;
  std::vector<std::string> diagnostics;
  std::vector<std::string> files;
};

bool ParsePrefixedLine(const std::string& line, const std::string& prefix, std::string* value_out) {
  if (!line.starts_with(prefix) || value_out == nullptr) {
    return false;
  }
  *value_out = line.substr(prefix.size());
  return true;
}

bool LoadMountState(const std::filesystem::path& state_file, MountState* state, std::string* error_out) {
  if (state == nullptr) {
    if (error_out != nullptr) {
      *error_out = "Invalid mount state output";
    }
    return false;
  }

  std::ifstream in(state_file, std::ios::binary);
  if (!in) {
    if (error_out != nullptr) {
      *error_out = "Cannot open mount state file";
    }
    return false;
  }

  std::vector<std::string> lines;
  std::string line;
  while (std::getline(in, line)) {
    lines.push_back(line);
  }

  if (lines.size() < 4) {
    if (error_out != nullptr) {
      *error_out = "Mount state is incomplete";
    }
    return false;
  }

  if (lines[0] != "VERSION=1") {
    if (error_out != nullptr) {
      *error_out = "Unsupported mount state version";
    }
    return false;
  }

  MountState parsed;
  if (!ParsePrefixedLine(lines[1], "MOUNT_POINT=", &parsed.mount_point) ||
      !ParsePrefixedLine(lines[2], "IMAGE_PATH=", &parsed.image_path) ||
      !ParsePrefixedLine(lines[3], "BACKEND=", &parsed.backend_name)) {
    if (error_out != nullptr) {
      *error_out = "Mount state header is invalid";
    }
    return false;
  }

  if (!IsValidMountPoint(parsed.mount_point) || parsed.image_path.empty() || parsed.backend_name.empty()) {
    if (error_out != nullptr) {
      *error_out = "Mount state values are invalid";
    }
    return false;
  }

  for (std::size_t i = 4; i < lines.size(); ++i) {
    std::string value;
    if (ParsePrefixedLine(lines[i], "FILE=", &value)) {
      if (!value.empty()) {
        parsed.files.push_back(value);
      }
      continue;
    }
    if (ParsePrefixedLine(lines[i], "DIAG=", &value)) {
      if (!value.empty()) {
        parsed.diagnostics.push_back(value);
      }
      continue;
    }
    if (ParsePrefixedLine(lines[i], "TELEMETRY_JSONL=", &value)) {
      parsed.telemetry_jsonl_path = value;
      continue;
    }

    if (error_out != nullptr) {
      *error_out = "Mount state file list is invalid";
    }
    return false;
  }

  *state = std::move(parsed);
  return true;
}

bool SaveMountState(const std::filesystem::path& state_file, const MountState& state, std::string* error_out) {
  const auto temp_file = state_file.string() + ".tmp";
  {
    std::ofstream out(temp_file, std::ios::binary | std::ios::trunc);
    if (!out) {
      if (error_out != nullptr) {
        *error_out = "Cannot write mount state temp file";
      }
      return false;
    }

    out << "VERSION=1\n";
    out << "MOUNT_POINT=" << state.mount_point << "\n";
    out << "IMAGE_PATH=" << state.image_path << "\n";
    out << "BACKEND=" << state.backend_name << "\n";
    out << "TELEMETRY_JSONL=" << state.telemetry_jsonl_path << "\n";
    for (const auto& diag : state.diagnostics) {
      out << "DIAG=" << diag << "\n";
    }
    for (const auto& file : state.files) {
      out << "FILE=" << file << "\n";
    }

    if (!out) {
      if (error_out != nullptr) {
        *error_out = "Failed while writing mount state";
      }
      return false;
    }
  }

  std::error_code ec;
  std::filesystem::rename(temp_file, state_file, ec);
  if (ec) {
    if (error_out != nullptr) {
      *error_out = "Cannot finalize mount state file";
    }
    std::filesystem::remove(temp_file, ec);
    return false;
  }

  return true;
}

std::filesystem::path DetectRepoRootFromExecutable() {
  std::error_code ec;
  const auto exe_path = std::filesystem::canonical(std::filesystem::path("."), ec);
  if (!ec) {
    auto probe = exe_path;
    while (!probe.empty()) {
      if (std::filesystem::exists(probe / "VERSION") && std::filesystem::exists(probe / "CMakeLists.txt")) {
        return probe;
      }
      const auto parent = probe.parent_path();
      if (parent == probe) {
        break;
      }
      probe = parent;
    }
  }

  return {};
}

std::string TrimAsciiWhitespace(std::string value) {
  auto is_space = [](unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
  };

  while (!value.empty() && is_space(static_cast<unsigned char>(value.front()))) {
    value.erase(value.begin());
  }
  while (!value.empty() && is_space(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }
  return value;
}

bool ReadProjectVersion(std::string* version_out, std::string* error_out) {
  if (version_out == nullptr || error_out == nullptr) {
    return false;
  }

  const auto root = DetectRepoRootFromExecutable();
  if (root.empty()) {
    *error_out = "Cannot locate repository root";
    return false;
  }

  const auto version_path = root / "VERSION";
  std::ifstream in(version_path, std::ios::binary);
  if (!in) {
    *error_out = "Cannot open VERSION file";
    return false;
  }

  std::string line;
  if (!std::getline(in, line)) {
    *error_out = "VERSION file is empty";
    return false;
  }

  line = TrimAsciiWhitespace(line);
  if (line.empty()) {
    *error_out = "VERSION value is empty";
    return false;
  }

  *version_out = std::move(line);
  return true;
}

bool PrepareMountedFilesystem(std::string mount_point,
                             WinFspFilesystem* fs_out,
                             MountState* state_out,
                             std::string* normalized_mount_out,
                             std::string* error_out) {
  if (fs_out == nullptr || state_out == nullptr || error_out == nullptr) {
    return false;
  }

  mount_point = NormalizeMountPoint(std::move(mount_point));
  if (!IsValidMountPoint(mount_point)) {
    *error_out = "invalid mount point, expected format X:";
    return false;
  }

  const auto state_file = MountStateFile(mount_point);
  if (!std::filesystem::exists(state_file)) {
    *error_out = "mount point is not mounted: " + mount_point;
    return false;
  }

  MountState state;
  std::string load_error;
  if (!LoadMountState(state_file, &state, &load_error)) {
    *error_out = "invalid mount state: " + load_error;
    return false;
  }
  if (state.mount_point != mount_point) {
    *error_out = "mount state mismatch for " + mount_point;
    return false;
  }

  if (!fs_out->MountReadOnly(state.image_path, mount_point)) {
    *error_out = fs_out->LastError();
    return false;
  }

  *state_out = std::move(state);
  if (normalized_mount_out != nullptr) {
    *normalized_mount_out = std::move(mount_point);
  }
  return true;
}

int CmdMountWithBackend(const std::string& image_path,
                       std::string mount_point,
                       const std::string& backend_name);
int CmdBackendDiag(const std::string& image_path, const std::string& backend_name, bool as_json);
int CmdBackendDiagMounted(std::string mount_point, bool as_json);
int CmdTelemetryDumpMounted(std::string mount_point);
int CmdTelemetryClearMounted(std::string mount_point);
int CmdTelemetryListMounted(std::string mount_point);
int CmdTelemetryStatsMounted(std::string mount_point, bool as_json);

struct TelemetryWherePredicate {
  enum class Kind {
    kEventEquals,
    kEventIEquals,
    kEventIContains,
    kEventStartsWith,
    kEventEndsWith,
    kEventPrefix,
    kEventSuffix,
    kEventContains,
    kDetailPrefix,
    kDetailSuffix,
    kDetailContains,
    kDetailIContains,
    kSuccessEquals,
  };

  Kind kind = Kind::kEventEquals;
  std::string value;
  int success = -1;
};

struct TelemetryWhereExpression {
  struct CompiledToken {
    enum class Kind {
      kPredicate,
      kNot,
      kAnd,
      kOr,
    };

    Kind kind = Kind::kPredicate;
    TelemetryWherePredicate predicate;
  };

  std::vector<CompiledToken> rpn;
};

struct TelemetryDumpOptions {
  enum class SelectorMode {
    kAll,
    kAny,
  };

  std::vector<std::string> include_events;
  std::vector<std::string> exclude_events;
  std::string event_prefix;
  std::string event_contains;
  std::string where_expression;
  std::string where_expression_normalized;
  TelemetryWhereExpression where_compiled;
  SelectorMode selector_mode = SelectorMode::kAll;
  int success_filter = -1;
  std::size_t tail = 0;
  std::size_t offset = 0;
  std::size_t limit = 0;
  bool as_json = false;
  bool bundle = false;
  bool explain = false;
  bool where_enabled = false;
};

std::string UpperAscii(std::string value) {
  for (char& c : value) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return value;
}

std::string LowerAscii(std::string value) {
  for (char& c : value) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return value;
}

bool TokenizeWhereExpression(const std::string& expression,
                             std::vector<std::string>* tokens_out,
                             std::string* error_out) {
  if (tokens_out == nullptr || error_out == nullptr) {
    return false;
  }

  tokens_out->clear();
  std::string current;
  bool in_quotes = false;
  bool escaped = false;
  for (char c : expression) {
    if (in_quotes) {
      current.push_back(c);
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        in_quotes = false;
      }
      continue;
    }

    if (c == '"') {
      current.push_back(c);
      in_quotes = true;
      escaped = false;
      continue;
    }

    if (c == '(' || c == ')') {
      if (!current.empty()) {
        tokens_out->push_back(current);
        current.clear();
      }
      tokens_out->push_back(std::string(1, c));
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!current.empty()) {
        tokens_out->push_back(current);
        current.clear();
      }
      continue;
    }
    current.push_back(c);
  }
  if (!current.empty()) {
    tokens_out->push_back(current);
  }

  if (in_quotes) {
    *error_out = "where expression has unterminated quoted value";
    return false;
  }

  if (tokens_out->empty()) {
    *error_out = "where expression cannot be empty";
    return false;
  }
  return true;
}

int WhereOperatorPrecedence(const std::string& op_upper) {
  if (op_upper == "NOT") {
    return 3;
  }
  if (op_upper == "AND") {
    return 2;
  }
  if (op_upper == "OR") {
    return 1;
  }
  return 0;
}

std::string EscapeWhereValue(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 4);
  for (char c : value) {
    if (c == '\\' || c == '"') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  return out;
}

bool ParseWhereValueLiteral(const std::string& raw,
                            std::string* value_out,
                            bool* quoted_out,
                            std::string* error_out) {
  if (value_out == nullptr || quoted_out == nullptr || error_out == nullptr) {
    return false;
  }

  const std::string trimmed = TrimAsciiWhitespace(raw);
  if (trimmed.empty()) {
    *error_out = "where predicate value cannot be empty";
    return false;
  }

  if (trimmed.front() != '"') {
    *value_out = trimmed;
    *quoted_out = false;
    return true;
  }

  if (trimmed.size() < 2 || trimmed.back() != '"') {
    *error_out = "where quoted value must end with \"";
    return false;
  }

  std::string value;
  value.reserve(trimmed.size());
  bool escaped = false;
  for (std::size_t i = 1; i + 1 < trimmed.size(); ++i) {
    const char c = trimmed[i];
    if (escaped) {
      if (c == '\\' || c == '"') {
        value.push_back(c);
      } else {
        value.push_back('\\');
        value.push_back(c);
      }
      escaped = false;
      continue;
    }
    if (c == '\\') {
      escaped = true;
      continue;
    }
    value.push_back(c);
  }
  if (escaped) {
    *error_out = "where quoted value has trailing escape";
    return false;
  }

  *value_out = std::move(value);
  *quoted_out = true;
  return true;
}

std::string FormatWhereValueNormalized(const std::string& value, bool prefer_quoted) {
  bool need_quote = prefer_quoted;
  for (char c : value) {
    if (std::isspace(static_cast<unsigned char>(c)) || c == '(' || c == ')' || c == '"') {
      need_quote = true;
      break;
    }
  }
  if (!need_quote) {
    return value;
  }
  return "\"" + EscapeWhereValue(value) + "\"";
}

bool JsonStringFieldContains(const std::string& line,
                             const std::string& field_name,
                             const std::string& needle);
bool JsonStringFieldIContains(const std::string& line,
                              const std::string& field_name,
                              const std::string& needle);
bool JsonStringFieldStartsWith(const std::string& line,
                               const std::string& field_name,
                               const std::string& prefix);
bool JsonStringFieldEndsWith(const std::string& line,
                             const std::string& field_name,
                             const std::string& suffix);

bool ParseWherePredicateToken(const std::string& token,
                              TelemetryWherePredicate* predicate_out,
                              std::string* normalized_out,
                              std::string* error_out) {
  if (predicate_out == nullptr || normalized_out == nullptr || error_out == nullptr) {
    return false;
  }

  auto parse_value = [&](const std::string& prefix, TelemetryWherePredicate::Kind kind,
                         const std::string& normalized_prefix) -> bool {
    if (!token.starts_with(prefix)) {
      return false;
    }
    std::string parsed_value;
    bool was_quoted = false;
    if (!ParseWhereValueLiteral(token.substr(prefix.size()), &parsed_value, &was_quoted, error_out)) {
      return true;
    }
    predicate_out->kind = kind;
    predicate_out->value = parsed_value;
    *normalized_out = normalized_prefix + FormatWhereValueNormalized(parsed_value, was_quoted);
    return true;
  };

  if (parse_value("event==", TelemetryWherePredicate::Kind::kEventEquals, "event==")) {
    return error_out->empty();
  }
  if (parse_value("event_ieq==", TelemetryWherePredicate::Kind::kEventIEquals, "event_ieq==")) {
    return error_out->empty();
  }
  if (parse_value("event_icontains==", TelemetryWherePredicate::Kind::kEventIContains,
                  "event_icontains==")) {
    return error_out->empty();
  }
  if (parse_value("event_starts_with==", TelemetryWherePredicate::Kind::kEventStartsWith,
                  "event_starts_with==")) {
    return error_out->empty();
  }
  if (parse_value("event_ends_with==", TelemetryWherePredicate::Kind::kEventEndsWith,
                  "event_ends_with==")) {
    return error_out->empty();
  }
  if (parse_value("event_prefix==", TelemetryWherePredicate::Kind::kEventPrefix, "event_prefix==")) {
    return error_out->empty();
  }
  if (parse_value("event_suffix==", TelemetryWherePredicate::Kind::kEventSuffix, "event_suffix==")) {
    return error_out->empty();
  }
  if (parse_value("event_contains==", TelemetryWherePredicate::Kind::kEventContains,
                  "event_contains==")) {
    return error_out->empty();
  }
  if (parse_value("detail_contains==", TelemetryWherePredicate::Kind::kDetailContains,
                  "detail_contains==")) {
    return error_out->empty();
  }
  if (parse_value("detail_icontains==", TelemetryWherePredicate::Kind::kDetailIContains,
                  "detail_icontains==")) {
    return error_out->empty();
  }
  if (parse_value("detail_prefix==", TelemetryWherePredicate::Kind::kDetailPrefix,
                  "detail_prefix==")) {
    return error_out->empty();
  }
  if (parse_value("detail_suffix==", TelemetryWherePredicate::Kind::kDetailSuffix,
                  "detail_suffix==")) {
    return error_out->empty();
  }

  if (token.starts_with("success==")) {
    std::string parsed_value;
    bool was_quoted = false;
    if (!ParseWhereValueLiteral(token.substr(std::string("success==").size()), &parsed_value, &was_quoted,
                                error_out)) {
      return false;
    }
    const std::string raw = parsed_value;
    if (raw == "true") {
      predicate_out->kind = TelemetryWherePredicate::Kind::kSuccessEquals;
      predicate_out->success = 1;
      *normalized_out = "success==true";
      return true;
    }
    if (raw == "false") {
      predicate_out->kind = TelemetryWherePredicate::Kind::kSuccessEquals;
      predicate_out->success = 0;
      *normalized_out = "success==false";
      return true;
    }
    *error_out = "where success predicate must be success==true or success==false";
    return false;
  }

  *error_out = "unsupported where predicate token: " + token;
  return false;
}

bool ParseWhereExpression(const std::string& expression,
                          TelemetryWhereExpression* where_out,
                          std::string* normalized_out,
                          std::string* error_out) {
  if (where_out == nullptr || normalized_out == nullptr || error_out == nullptr) {
    return false;
  }

  const std::string trimmed = TrimAsciiWhitespace(expression);
  if (trimmed.empty()) {
    *error_out = "where expression cannot be empty";
    return false;
  }

  std::vector<std::string> tokens;
  if (!TokenizeWhereExpression(trimmed, &tokens, error_out)) {
    return false;
  }

  std::vector<TelemetryWhereExpression::CompiledToken> output;
  std::vector<std::string> op_stack;
  std::string normalized;
  bool expect_predicate = true;

  for (const auto& t : tokens) {
    if (t == "(") {
      if (!expect_predicate) {
        *error_out = "where expression missing operator before (";
        return false;
      }
      op_stack.push_back(t);
      if (!normalized.empty()) {
        normalized += " ";
      }
      normalized += "(";
      continue;
    }

    if (t == ")") {
      if (expect_predicate) {
        *error_out = "where expression has empty group or trailing operator before )";
        return false;
      }
      bool found_open = false;
      while (!op_stack.empty()) {
        const std::string top = op_stack.back();
        op_stack.pop_back();
        if (top == "(") {
          found_open = true;
          break;
        }
        TelemetryWhereExpression::CompiledToken op_token;
        const std::string op_upper = UpperAscii(top);
        if (op_upper == "AND") {
          op_token.kind = TelemetryWhereExpression::CompiledToken::Kind::kAnd;
        } else if (op_upper == "OR") {
          op_token.kind = TelemetryWhereExpression::CompiledToken::Kind::kOr;
        } else {
          op_token.kind = TelemetryWhereExpression::CompiledToken::Kind::kNot;
        }
        output.push_back(op_token);
      }
      if (!found_open) {
        *error_out = "where expression has unmatched )";
        return false;
      }
      normalized += " )";
      continue;
    }

    const std::string upper = UpperAscii(t);
    if (upper == "NOT") {
      if (!expect_predicate) {
        *error_out = "where expression unexpected NOT after predicate";
        return false;
      }
      op_stack.push_back(upper);
      if (!normalized.empty() && normalized.back() != ' ') {
        normalized += " ";
      }
      normalized += upper;
      continue;
    }
    if (upper == "AND" || upper == "OR") {
      if (expect_predicate) {
        *error_out = "where expression expects predicate before operator " + upper;
        return false;
      }
      while (!op_stack.empty()) {
        const std::string top = UpperAscii(op_stack.back());
        if (top == "(") {
          break;
        }
        if (WhereOperatorPrecedence(top) < WhereOperatorPrecedence(upper)) {
          break;
        }
        const std::string popped = UpperAscii(op_stack.back());
        op_stack.pop_back();
        TelemetryWhereExpression::CompiledToken op_token;
        if (popped == "AND") {
          op_token.kind = TelemetryWhereExpression::CompiledToken::Kind::kAnd;
        } else if (popped == "OR") {
          op_token.kind = TelemetryWhereExpression::CompiledToken::Kind::kOr;
        } else {
          op_token.kind = TelemetryWhereExpression::CompiledToken::Kind::kNot;
        }
        output.push_back(op_token);
      }
      op_stack.push_back(upper);
      normalized += " " + upper;
      expect_predicate = true;
      continue;
    }

    if (!expect_predicate) {
      *error_out = "where expression missing operator before token: " + t;
      return false;
    }

    TelemetryWherePredicate predicate;
    std::string normalized_predicate;
    std::string parse_error;
    if (!ParseWherePredicateToken(t, &predicate, &normalized_predicate, &parse_error)) {
      *error_out = parse_error;
      return false;
    }

    TelemetryWhereExpression::CompiledToken predicate_token;
    predicate_token.kind = TelemetryWhereExpression::CompiledToken::Kind::kPredicate;
    predicate_token.predicate = std::move(predicate);
    output.push_back(std::move(predicate_token));

    if (!normalized.empty() && normalized.back() != '(' && normalized.back() != ' ') {
      normalized += " ";
    }
    normalized += normalized_predicate;
    expect_predicate = false;
  }

  if (expect_predicate) {
    *error_out = "where expression cannot end with logical operator";
    return false;
  }

  while (!op_stack.empty()) {
    const std::string top = UpperAscii(op_stack.back());
    op_stack.pop_back();
    if (top == "(") {
      *error_out = "where expression has unmatched (";
      return false;
    }
    TelemetryWhereExpression::CompiledToken op_token;
    if (top == "AND") {
      op_token.kind = TelemetryWhereExpression::CompiledToken::Kind::kAnd;
    } else if (top == "OR") {
      op_token.kind = TelemetryWhereExpression::CompiledToken::Kind::kOr;
    } else {
      op_token.kind = TelemetryWhereExpression::CompiledToken::Kind::kNot;
    }
    output.push_back(op_token);
  }

  if (output.empty()) {
    *error_out = "where expression is malformed";
    return false;
  }

  where_out->rpn = std::move(output);
  *normalized_out = TrimAsciiWhitespace(std::move(normalized));
  return true;
}

bool EvaluateWherePredicate(const std::string& line,
                           const std::string& name,
                           int success,
                           const TelemetryWherePredicate& predicate) {
  switch (predicate.kind) {
    case TelemetryWherePredicate::Kind::kEventEquals:
      return name == predicate.value;
    case TelemetryWherePredicate::Kind::kEventIEquals:
      return LowerAscii(name) == LowerAscii(predicate.value);
    case TelemetryWherePredicate::Kind::kEventIContains:
      return LowerAscii(name).find(LowerAscii(predicate.value)) != std::string::npos;
    case TelemetryWherePredicate::Kind::kEventStartsWith:
      return name.starts_with(predicate.value);
    case TelemetryWherePredicate::Kind::kEventEndsWith:
      return name.size() >= predicate.value.size() &&
             name.compare(name.size() - predicate.value.size(), predicate.value.size(), predicate.value) == 0;
    case TelemetryWherePredicate::Kind::kEventPrefix:
      return name.starts_with(predicate.value);
    case TelemetryWherePredicate::Kind::kEventSuffix:
      return name.size() >= predicate.value.size() &&
             name.compare(name.size() - predicate.value.size(), predicate.value.size(), predicate.value) == 0;
    case TelemetryWherePredicate::Kind::kEventContains:
      return name.find(predicate.value) != std::string::npos;
    case TelemetryWherePredicate::Kind::kDetailPrefix:
      return JsonStringFieldStartsWith(line, "detail", predicate.value);
    case TelemetryWherePredicate::Kind::kDetailSuffix:
      return JsonStringFieldEndsWith(line, "detail", predicate.value);
    case TelemetryWherePredicate::Kind::kDetailContains:
      return JsonStringFieldContains(line, "detail", predicate.value);
    case TelemetryWherePredicate::Kind::kDetailIContains:
      return JsonStringFieldIContains(line, "detail", predicate.value);
    case TelemetryWherePredicate::Kind::kSuccessEquals:
      return success != -1 && success == predicate.success;
  }
  return false;
}

bool EvaluateWhereExpression(const std::string& line,
                            const std::string& name,
                            int success,
                            const TelemetryWhereExpression& where_expression) {
  if (where_expression.rpn.empty()) {
    return true;
  }

  std::vector<bool> eval_stack;
  for (const auto& token : where_expression.rpn) {
    if (token.kind == TelemetryWhereExpression::CompiledToken::Kind::kPredicate) {
      eval_stack.push_back(EvaluateWherePredicate(line, name, success, token.predicate));
      continue;
    }

    if (token.kind == TelemetryWhereExpression::CompiledToken::Kind::kNot) {
      if (eval_stack.empty()) {
        return false;
      }
      eval_stack.back() = !eval_stack.back();
      continue;
    }

    if (eval_stack.size() < 2) {
      return false;
    }

    const bool rhs = eval_stack.back();
    eval_stack.pop_back();
    const bool lhs = eval_stack.back();
    eval_stack.pop_back();

    if (token.kind == TelemetryWhereExpression::CompiledToken::Kind::kAnd) {
      eval_stack.push_back(lhs && rhs);
    } else {
      eval_stack.push_back(lhs || rhs);
    }
  }

  if (eval_stack.size() != 1) {
    return false;
  }
  return eval_stack[0];
}

bool HasPositiveSelectors(const TelemetryDumpOptions& opt) {
  return !opt.include_events.empty() || !opt.event_prefix.empty() || !opt.event_contains.empty();
}

void NormalizeStringList(std::vector<std::string>* values) {
  if (values == nullptr) {
    return;
  }

  std::vector<std::string> filtered;
  filtered.reserve(values->size());
  for (const auto& value : *values) {
    const auto trimmed = TrimAsciiWhitespace(value);
    if (!trimmed.empty()) {
      filtered.push_back(trimmed);
    }
  }

  std::sort(filtered.begin(), filtered.end());
  filtered.erase(std::unique(filtered.begin(), filtered.end()), filtered.end());
  *values = std::move(filtered);
}

bool NormalizeTelemetryDumpOptions(TelemetryDumpOptions* opt, std::string* error_out) {
  if (opt == nullptr || error_out == nullptr) {
    return false;
  }

  NormalizeStringList(&opt->include_events);
  NormalizeStringList(&opt->exclude_events);
  opt->event_prefix = TrimAsciiWhitespace(opt->event_prefix);
  opt->event_contains = TrimAsciiWhitespace(opt->event_contains);

  if (opt->where_enabled) {
    TelemetryWhereExpression parsed_where;
    std::string normalized_where;
    std::string parse_error;
    if (!ParseWhereExpression(opt->where_expression, &parsed_where, &normalized_where, &parse_error)) {
      *error_out = parse_error;
      return false;
    }
    opt->where_expression_normalized = std::move(normalized_where);
    opt->where_compiled = std::move(parsed_where);
  } else {
    opt->where_expression_normalized.clear();
    opt->where_compiled = TelemetryWhereExpression{};
  }

  if (!HasPositiveSelectors(*opt)) {
    opt->selector_mode = TelemetryDumpOptions::SelectorMode::kAll;
  }
  return true;
}

int CmdTelemetryDumpMountedFiltered(std::string mount_point, const TelemetryDumpOptions& opt);

std::string EscapeJson(const std::string& value) {
  std::string out;
  out.reserve(value.size() + 8);
  for (char c : value) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out.push_back(c);
        break;
    }
  }
  return out;
}

std::vector<std::string> SplitNonEmptyLines(const std::string& block) {
  std::vector<std::string> lines;
  std::size_t cursor = 0;
  while (cursor <= block.size()) {
    const std::size_t next = block.find('\n', cursor);
    const std::size_t end = next == std::string::npos ? block.size() : next;
    const std::string line = block.substr(cursor, end - cursor);
    if (!line.empty()) {
      lines.push_back(line);
    }
    if (next == std::string::npos) {
      break;
    }
    cursor = next + 1;
  }
  return lines;
}

std::vector<std::filesystem::path> CollectTelemetryFiles(const std::filesystem::path& base,
                                                         std::size_t max_files = 16) {
  std::vector<std::filesystem::path> files;
  if (std::filesystem::exists(base)) {
    files.push_back(base);
  }
  for (std::size_t i = 1; i <= max_files; ++i) {
    const auto candidate = base.string() + "." + std::to_string(i);
    if (!std::filesystem::exists(candidate)) {
      break;
    }
    files.emplace_back(candidate);
  }
  return files;
}

std::string ExtractJsonStringField(const std::string& line, const std::string& field_name) {
  const std::string key = "\"" + field_name + "\":\"";
  const std::size_t pos = line.find(key);
  if (pos == std::string::npos) {
    return "";
  }
  const std::size_t start = pos + key.size();
  std::size_t i = start;
  bool escaped = false;
  for (; i < line.size(); ++i) {
    const char c = line[i];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (c == '\\') {
      escaped = true;
      continue;
    }
    if (c == '"') {
      break;
    }
  }
  if (i >= line.size()) {
    return "";
  }
  return line.substr(start, i - start);
}

int ExtractJsonBoolField(const std::string& line, const std::string& field_name) {
  const std::string key = "\"" + field_name + "\":";
  const std::size_t pos = line.find(key);
  if (pos == std::string::npos) {
    return -1;
  }
  const std::size_t start = pos + key.size();
  if (line.compare(start, 4, "true") == 0) {
    return 1;
  }
  if (line.compare(start, 5, "false") == 0) {
    return 0;
  }
  return -1;
}

bool JsonStringFieldContains(const std::string& line,
                             const std::string& field_name,
                             const std::string& needle) {
  if (needle.empty()) {
    return true;
  }
  const std::string value = ExtractJsonStringField(line, field_name);
  return value.find(needle) != std::string::npos;
}

bool JsonStringFieldIContains(const std::string& line,
                              const std::string& field_name,
                              const std::string& needle) {
  const std::string value = ExtractJsonStringField(line, field_name);
  return LowerAscii(value).find(LowerAscii(needle)) != std::string::npos;
}

bool JsonStringFieldStartsWith(const std::string& line,
                               const std::string& field_name,
                               const std::string& prefix) {
  const std::string value = ExtractJsonStringField(line, field_name);
  return value.starts_with(prefix);
}

bool JsonStringFieldEndsWith(const std::string& line,
                             const std::string& field_name,
                             const std::string& suffix) {
  const std::string value = ExtractJsonStringField(line, field_name);
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::uint64_t StableWhereHash(const std::string& normalized_where) {
  return static_cast<std::uint64_t>(std::hash<std::string>{}(normalized_where));
}

std::uint64_t ComputeWhereFeatureMask(const TelemetryWhereExpression& where_expression) {
  std::uint64_t mask = 0;
  for (const auto& token : where_expression.rpn) {
    if (token.kind == TelemetryWhereExpression::CompiledToken::Kind::kNot) {
      mask |= (1ull << 0);
      continue;
    }
    if (token.kind == TelemetryWhereExpression::CompiledToken::Kind::kAnd) {
      mask |= (1ull << 1);
      continue;
    }
    if (token.kind == TelemetryWhereExpression::CompiledToken::Kind::kOr) {
      mask |= (1ull << 2);
      continue;
    }

    switch (token.predicate.kind) {
      case TelemetryWherePredicate::Kind::kEventEquals:
      case TelemetryWherePredicate::Kind::kEventIEquals:
        mask |= (1ull << 3);
        break;
      case TelemetryWherePredicate::Kind::kEventIContains:
      case TelemetryWherePredicate::Kind::kEventStartsWith:
      case TelemetryWherePredicate::Kind::kEventEndsWith:
      case TelemetryWherePredicate::Kind::kEventPrefix:
      case TelemetryWherePredicate::Kind::kEventSuffix:
      case TelemetryWherePredicate::Kind::kEventContains:
        mask |= (1ull << 4);
        break;
      case TelemetryWherePredicate::Kind::kDetailPrefix:
      case TelemetryWherePredicate::Kind::kDetailSuffix:
      case TelemetryWherePredicate::Kind::kDetailContains:
      case TelemetryWherePredicate::Kind::kDetailIContains:
        mask |= (1ull << 5);
        break;
      case TelemetryWherePredicate::Kind::kSuccessEquals:
        mask |= (1ull << 6);
        break;
    }
  }
  return mask;
}

bool TelemetryLineMatchesFilter(const std::string& line, const TelemetryDumpOptions& opt) {
  const std::string name = ExtractJsonStringField(line, "name");
  const int success = ExtractJsonBoolField(line, "success");

  if (opt.where_enabled) {
    if (!EvaluateWhereExpression(line, name, success, opt.where_compiled)) {
      return false;
    }
  }

  for (const auto& exclude_event : opt.exclude_events) {
    if (name == exclude_event) {
      return false;
    }
  }

  const bool has_exact = !opt.include_events.empty();
  const bool has_prefix = !opt.event_prefix.empty();
  const bool has_contains = !opt.event_contains.empty();

  bool matches_exact = false;
  for (const auto& include_event : opt.include_events) {
    if (name == include_event) {
      matches_exact = true;
      break;
    }
  }

  const bool matches_prefix = has_prefix && name.starts_with(opt.event_prefix);
  const bool matches_contains = has_contains && name.find(opt.event_contains) != std::string::npos;

  if (opt.selector_mode == TelemetryDumpOptions::SelectorMode::kAll) {
    if (has_exact && !matches_exact) {
      return false;
    }
    if (has_prefix && !matches_prefix) {
      return false;
    }
    if (has_contains && !matches_contains) {
      return false;
    }
  } else {
    const bool has_positive_selector = HasPositiveSelectors(opt);
    if (has_positive_selector && !(matches_exact || matches_prefix || matches_contains)) {
      return false;
    }
  }

  if (opt.success_filter != -1) {
    if (success != opt.success_filter) {
      return false;
    }
  }
  return true;
}

void PrintUsage() {
  std::cout << "JDrive64 CLI\n"
            << "Usage:\n"
            << "  jdrive64 info <image.d64>\n"
            << "  jdrive64 version\n"
            << "  jdrive64 ls <image.d64>\n"
            << "  jdrive64 extract <image.d64> [output_dir]\n"
            << "  jdrive64 mount <image.d64> <drive_letter:>\n"
            << "  jdrive64 mount <image.d64> <drive_letter:> --backend <winfsp|kdrv>\n"
            << "  jdrive64 mounts\n"
            << "  jdrive64 unmount <drive_letter:>\n"
            << "  jdrive64 dir-mounted <drive_letter:>\n"
            << "  jdrive64 read-mounted <drive_letter:> <name.ext>\n"
            << "  jdrive64 volume-mounted <drive_letter:>\n"
            << "  jdrive64 stats-mounted <drive_letter:>\n"
            << "  jdrive64 check-mounted <drive_letter:>\n"
            << "  jdrive64 backend-diag <image.d64> [--backend <winfsp|kdrv>] [--json]\n"
            << "  jdrive64 backend-diag-mounted <drive_letter:> [--json]\n"
            << "  jdrive64 telemetry-dump-mounted <drive_letter:> [--event <name>] [--exclude-event <name>] [--event-prefix <prefix>] [--event-contains <text>] [--selector-mode <all|any>] [--where <expr>] [--success <true|false>] [--tail N] [--offset N] [--limit N] [--json] [--bundle] [--explain]\n"
            << "  jdrive64 telemetry-clear-mounted <drive_letter:>\n"
            << "  jdrive64 telemetry-list-mounted <drive_letter:>\n"
            << "  jdrive64 telemetry-stats-mounted <drive_letter:> [--json]\n"
            << "  jdrive64 winfsp-preflight <image.d64> <drive_letter:>\n"
            << "  jdrive64 write-add <image.d64> <host_file> <name.ext>\n"
            << "  jdrive64 write-del <image.d64> <name.ext>\n"
            << "  jdrive64 write-ren <image.d64> <old.ext> <new.ext>\n";
}

std::string SanitizeFilename(const std::string& base_name, const std::string& ext) {
  std::string out;
  out.reserve(base_name.size() + ext.size() + 1);

  const std::string invalid = "<>:\\|?*\"/";
  for (char c : base_name) {
    if (c < 32 || invalid.find(c) != std::string::npos) {
      out.push_back('_');
    } else {
      out.push_back(c);
    }
  }

  if (out.empty()) {
    out = "UNNAMED";
  }

  out += ".";
  out += ext;
  return out;
}

int CmdInfo(const std::string& image_path) {
  DiskImageSession session;
  if (!session.Open(image_path)) {
    std::cerr << "Error: " << session.LastError() << "\n";
    return 1;
  }

  const std::uint32_t free_blocks = session.Bam().FreeBlocks();
  const std::uint32_t used_blocks = kD64TotalBlocks - free_blocks;
  const std::uint64_t capacity_bytes =
      static_cast<std::uint64_t>(kD64TotalBlocks) * kD64BlockSizeBytes;
  const std::uint64_t free_bytes = static_cast<std::uint64_t>(free_blocks) * kD64BlockSizeBytes;
  const std::uint64_t used_bytes = static_cast<std::uint64_t>(used_blocks) * kD64BlockSizeBytes;

  std::cout << "Label      : " << session.Bam().DiskName() << "\n";
  std::cout << "Disk ID    : " << session.Bam().DiskId() << "\n";
  std::cout << "DOS Type   : " << session.Bam().DosType() << "\n";
  std::cout << "FileSystem : JDrive64\n";
  std::cout << "Block Size : " << kD64BlockSizeBytes << " bytes\n";
  std::cout << "Capacity   : " << kD64TotalBlocks << " blocks (" << capacity_bytes << " bytes)\n";
  std::cout << "Used       : " << used_blocks << " blocks (" << used_bytes << " bytes)\n";
  std::cout << "Free       : " << free_blocks << " blocks (" << free_bytes << " bytes)\n";
  return 0;
}

int CmdVersion() {
  std::string version;
  std::string error;
  if (!ReadProjectVersion(&version, &error)) {
    std::cerr << "Error: " << error << "\n";
    return 1;
  }

  std::cout << "JDrive64 " << version << "\n";
  return 0;
}

int CmdLs(const std::string& image_path) {
  DiskImageSession session;
  if (!session.Open(image_path)) {
    std::cerr << "Error: " << session.LastError() << "\n";
    return 1;
  }

  for (const auto& file : session.Catalog().Files()) {
    std::cout << file.windows_name << "  " << file.size_blocks << " blocks\n";
  }

  return 0;
}

int CmdExtract(const std::string& image_path, const std::string& output_dir_arg) {
  DiskImageSession session;
  if (!session.Open(image_path)) {
    std::cerr << "Error: " << session.LastError() << "\n";
    return 1;
  }

  std::filesystem::path output_dir;
  if (output_dir_arg.empty()) {
    const std::filesystem::path image(image_path);
    output_dir = image.stem().string() + "_extract";
  } else {
    output_dir = output_dir_arg;
  }

  std::error_code ec;
  std::filesystem::create_directories(output_dir, ec);
  if (ec) {
    std::cerr << "Error: cannot create output directory: " << output_dir.string() << "\n";
    return 1;
  }

  bool had_warnings = false;
  for (const auto& file : session.Catalog().Files()) {
    std::vector<std::uint8_t> file_data;
    if (!session.ReadFileByCatalogFile(file, &file_data)) {
      std::cerr << "Warning: cannot read " << file.display_name << ": " << session.LastError()
                << "\n";
      had_warnings = true;
      continue;
    }

    const auto file_name = SanitizeFilename(file.display_name, file.extension);
    const auto file_path = output_dir / file_name;
    std::ofstream out(file_path, std::ios::binary);
    if (!out) {
      std::cerr << "Warning: cannot write " << file_path.string() << "\n";
      had_warnings = true;
      continue;
    }

    if (!file_data.empty()) {
      out.write(reinterpret_cast<const char*>(file_data.data()),
                static_cast<std::streamsize>(file_data.size()));
    }

    if (!out) {
      std::cerr << "Warning: write failed for " << file_path.string() << "\n";
      had_warnings = true;
      continue;
    }

    std::cout << file_name << "\n";
  }

  return had_warnings ? 2 : 0;
}

int CmdMount(const std::string& image_path, std::string mount_point) {
  return CmdMountWithBackend(image_path, std::move(mount_point), "winfsp");
}

int CmdMountWithBackend(const std::string& image_path,
                       std::string mount_point,
                       const std::string& backend_name) {
  mount_point = NormalizeMountPoint(std::move(mount_point));
  if (!IsValidMountPoint(mount_point)) {
    std::cerr << "Error: invalid mount point, expected format X:\n";
    return 1;
  }

  std::error_code ec;
  std::filesystem::create_directories(MountStateRoot(), ec);
  if (ec) {
    std::cerr << "Error: cannot initialize mount state directory\n";
    return 1;
  }

  const auto state_file = MountStateFile(mount_point);
  if (std::filesystem::exists(state_file)) {
    std::cerr << "Error: mount point already marked as mounted: " << mount_point
              << " (unmount first)\n";
    return 1;
  }

  std::error_code fs_ec;
  auto resolved_image = std::filesystem::absolute(image_path, fs_ec);
  if (fs_ec) {
    resolved_image = image_path;
  }
  if (!std::filesystem::exists(resolved_image)) {
    std::cerr << "Error: image file does not exist: " << resolved_image.string() << "\n";
    return 1;
  }

  std::string normalized_backend;
  std::string backend_error;
  auto backend = jdrive64::CreateMountBackend(backend_name, &normalized_backend, &backend_error);
  if (backend == nullptr) {
    std::cerr << "Error: " << backend_error << "\n";
    return 1;
  }

  if (!backend->MountReadOnly(resolved_image.string(), mount_point)) {
    std::cerr << "Error: " << backend->LastError() << "\n";
    return 1;
  }

  if (!backend->HealthCheck()) {
    std::cerr << "Error: backend health check failed" << "\n";
    return 1;
  }

  MountState state;
  state.mount_point = mount_point;
  state.image_path = resolved_image.string();
  state.backend_name = normalized_backend;
  {
    const char* telemetry_env = std::getenv("JDRIVE64_TELEMETRY_JSONL");
    if (telemetry_env != nullptr) {
      state.telemetry_jsonl_path = TrimAsciiWhitespace(telemetry_env);
    }
  }
  {
    const std::string diag_block = backend->GetBackendDiagnosticsText();
    std::size_t cursor = 0;
    while (cursor <= diag_block.size()) {
      const std::size_t next = diag_block.find('\n', cursor);
      const std::size_t end = next == std::string::npos ? diag_block.size() : next;
      const std::string line = diag_block.substr(cursor, end - cursor);
      if (!line.empty()) {
        state.diagnostics.push_back(line);
      }
      if (next == std::string::npos) {
        break;
      }
      cursor = next + 1;
    }
  }
  state.files = backend->ReadDirectory();

  std::string save_error;
  if (!SaveMountState(state_file, state, &save_error)) {
    std::cerr << "Error: cannot persist mount state: " << save_error << "\n";
    return 1;
  }

  std::cout << "Mounted " << resolved_image.string() << " on " << mount_point
            << " (read-only, backend=" << normalized_backend << ")\n";
  const auto info = backend->GetVolumeInfoText();
  if (!info.empty()) {
    std::cout << info << "\n";
  }
  return 0;
}

int CmdMounts() {
  std::error_code ec;
  std::filesystem::create_directories(MountStateRoot(), ec);
  if (ec) {
    std::cerr << "Error: cannot access mount state directory\n";
    return 1;
  }

  std::vector<std::string> lines;
  for (const auto& entry : std::filesystem::directory_iterator(MountStateRoot(), ec)) {
    if (ec) {
      std::cerr << "Error: cannot enumerate mount state directory\n";
      return 1;
    }
    if (!entry.is_regular_file()) {
      continue;
    }

    MountState state;
    std::string load_error;
    if (!LoadMountState(entry.path(), &state, &load_error)) {
      continue;
    }
    if (!IsValidMountPoint(state.mount_point)) {
      continue;
    }

    lines.push_back(state.mount_point + " -> " + state.image_path);
  }

  std::sort(lines.begin(), lines.end());
  for (const auto& line : lines) {
    std::cout << line << "\n";
  }
  return 0;
}

int CmdUnmount(std::string mount_point) {
  mount_point = NormalizeMountPoint(std::move(mount_point));
  if (!IsValidMountPoint(mount_point)) {
    std::cerr << "Error: invalid mount point, expected format X:\n";
    return 1;
  }

  const auto state_file = MountStateFile(mount_point);
  if (!std::filesystem::exists(state_file)) {
    std::cerr << "Error: mount point is not mounted: " << mount_point << "\n";
    return 1;
  }

  MountState state;
  std::string load_error;
  if (!LoadMountState(state_file, &state, &load_error)) {
    std::cerr << "Error: invalid mount state: " << load_error << "\n";
    return 1;
  }
  if (state.mount_point != mount_point) {
    std::cerr << "Error: mount state mismatch for " << mount_point << "\n";
    return 1;
  }

  std::error_code ec;
  std::filesystem::remove(state_file, ec);
  if (ec) {
    std::cerr << "Error: failed to remove mount state file\n";
    return 1;
  }

  std::cout << "Unmounted " << mount_point << "\n";
  return 0;
}

int CmdDirMounted(std::string mount_point) {
  std::string error;
  MountState state;
  WinFspFilesystem fs;
  if (!PrepareMountedFilesystem(std::move(mount_point), &fs, &state, nullptr, &error)) {
    std::cerr << "Error: " << error << "\n";
    return 1;
  }

  for (const auto& name : fs.ReadDirectory()) {
    std::cout << name << "\n";
  }

  return 0;
}

int CmdReadMounted(std::string mount_point, const std::string& windows_name) {
  std::string error;
  MountState state;
  WinFspFilesystem fs;
  if (!PrepareMountedFilesystem(std::move(mount_point), &fs, &state, nullptr, &error)) {
    std::cerr << "Error: " << error << "\n";
    return 1;
  }

  std::vector<std::uint8_t> data;
  if (!fs.ReadFileByWindowsName(windows_name, &data)) {
    std::cerr << "Error: " << fs.LastError() << "\n";
    return 1;
  }

  std::cout.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
  return 0;
}

int CmdVolumeMounted(std::string mount_point) {
  std::string error;
  MountState state;
  WinFspFilesystem fs;
  if (!PrepareMountedFilesystem(std::move(mount_point), &fs, &state, nullptr, &error)) {
    std::cerr << "Error: " << error << "\n";
    return 1;
  }

  std::cout << fs.GetVolumeInfoText() << "\n";
  return 0;
}

int CmdStatsMounted(std::string mount_point) {
  std::string error;
  MountState state;
  WinFspFilesystem fs;
  if (!PrepareMountedFilesystem(std::move(mount_point), &fs, &state, nullptr, &error)) {
    std::cerr << "Error: " << error << "\n";
    return 1;
  }

  std::cout << fs.GetRuntimeStatsText() << "\n";
  return 0;
}

int CmdCheckMounted(std::string mount_point) {
  std::string error;
  std::string normalized_mount;
  MountState state;
  WinFspFilesystem fs;
  if (!PrepareMountedFilesystem(std::move(mount_point), &fs, &state, &normalized_mount, &error)) {
    std::cerr << "Error: " << error << "\n";
    return 1;
  }

  WinFspFilesystem::VolumeInfo volume;
  if (!fs.GetVolumeInfo(&volume)) {
    std::cerr << "Error: " << fs.LastError() << "\n";
    return 1;
  }

  const auto names = fs.ReadDirectory();

  std::cout << "Mount: " << normalized_mount << "\n";
  std::cout << "Image: " << state.image_path << "\n";
  std::cout << "Status: OK\n";
  std::cout << "Files: " << names.size() << "\n";
  std::cout << "Volume: " << volume.label << " (" << volume.free_blocks << "/"
            << volume.capacity_blocks << " free blocks)\n";
  return 0;
}

int CmdWinfspPreflight(const std::string& image_path, std::string mount_point) {
  mount_point = NormalizeMountPoint(std::move(mount_point));
  if (!IsValidMountPoint(mount_point)) {
    std::cerr << "Error: invalid mount point, expected format X:\n";
    return 1;
  }

  std::error_code ec;
  const auto resolved_image = std::filesystem::absolute(image_path, ec);
  if (ec || !std::filesystem::exists(resolved_image)) {
    std::cerr << "Error: image file does not exist: " << image_path << "\n";
    return 1;
  }

  WinFspRuntime runtime;
  if (!runtime.StartReadOnly(resolved_image.string(), mount_point)) {
    std::cerr << "Error: " << runtime.LastError() << "\n";
    return 1;
  }

  if (!runtime.Stop()) {
    std::cerr << "Error: " << runtime.LastError() << "\n";
    return 1;
  }

  std::cout << "WinFsp preflight OK for " << mount_point << "\n";
  return 0;
}

int CmdBackendDiag(const std::string& image_path, const std::string& backend_name, bool as_json) {
  std::error_code fs_ec;
  auto resolved_image = std::filesystem::absolute(image_path, fs_ec);
  if (fs_ec) {
    resolved_image = image_path;
  }
  if (!std::filesystem::exists(resolved_image)) {
    std::cerr << "Error: image file does not exist: " << resolved_image.string() << "\n";
    return 1;
  }

  std::string normalized_backend;
  std::string backend_error;
  auto backend = jdrive64::CreateMountBackend(backend_name, &normalized_backend, &backend_error);
  if (backend == nullptr) {
    std::cerr << "Error: " << backend_error << "\n";
    return 1;
  }

  const std::string diag = backend->GetBackendDiagnosticsText();
  const auto diag_lines = SplitNonEmptyLines(diag);
  if (as_json) {
    std::cout << "{\n";
    std::cout << "  \"image\": \"" << EscapeJson(resolved_image.string()) << "\",\n";
    std::cout << "  \"requested_backend\": \"" << EscapeJson(normalized_backend) << "\",\n";
    std::cout << "  \"diagnostics\": [";
    for (std::size_t i = 0; i < diag_lines.size(); ++i) {
      if (i != 0) {
        std::cout << ", ";
      }
      std::cout << "\"" << EscapeJson(diag_lines[i]) << "\"";
    }
    std::cout << "]\n";
    std::cout << "}\n";
  } else {
    std::cout << "Image: " << resolved_image.string() << "\n";
    std::cout << "RequestedBackend: " << normalized_backend << "\n";
    if (diag.empty()) {
      std::cout << "Diagnostics: unavailable\n";
    } else {
      std::cout << diag << "\n";
    }
  }
  return 0;
}

int CmdBackendDiagMounted(std::string mount_point, bool as_json) {
  mount_point = NormalizeMountPoint(std::move(mount_point));
  if (!IsValidMountPoint(mount_point)) {
    std::cerr << "Error: invalid mount point, expected format X:\n";
    return 1;
  }

  const auto state_file = MountStateFile(mount_point);
  if (!std::filesystem::exists(state_file)) {
    std::cerr << "Error: mount point is not mounted: " << mount_point << "\n";
    return 1;
  }

  MountState state;
  std::string load_error;
  if (!LoadMountState(state_file, &state, &load_error)) {
    std::cerr << "Error: invalid mount state: " << load_error << "\n";
    return 1;
  }

  if (as_json) {
    std::cout << "{\n";
    std::cout << "  \"mount\": \"" << EscapeJson(state.mount_point) << "\",\n";
    std::cout << "  \"image\": \"" << EscapeJson(state.image_path) << "\",\n";
    std::cout << "  \"backend\": \"" << EscapeJson(state.backend_name) << "\",\n";
    std::cout << "  \"telemetry_jsonl\": \"" << EscapeJson(state.telemetry_jsonl_path) << "\",\n";
    std::cout << "  \"diagnostics\": [";
    for (std::size_t i = 0; i < state.diagnostics.size(); ++i) {
      if (i != 0) {
        std::cout << ", ";
      }
      std::cout << "\"" << EscapeJson(state.diagnostics[i]) << "\"";
    }
    std::cout << "]\n";
    std::cout << "}\n";
  } else {
    std::cout << "Mount: " << state.mount_point << "\n";
    std::cout << "Image: " << state.image_path << "\n";
    std::cout << "Backend: " << state.backend_name << "\n";
    if (!state.telemetry_jsonl_path.empty()) {
      std::cout << "TelemetryJsonl: " << state.telemetry_jsonl_path << "\n";
    }
    if (state.diagnostics.empty()) {
      std::cout << "Diagnostics: unavailable\n";
    } else {
      for (const auto& line : state.diagnostics) {
        std::cout << line << "\n";
      }
    }
  }
  return 0;
}

int CmdTelemetryDumpMounted(std::string mount_point) {
  mount_point = NormalizeMountPoint(std::move(mount_point));
  if (!IsValidMountPoint(mount_point)) {
    std::cerr << "Error: invalid mount point, expected format X:\n";
    return 1;
  }

  const auto state_file = MountStateFile(mount_point);
  if (!std::filesystem::exists(state_file)) {
    std::cerr << "Error: mount point is not mounted: " << mount_point << "\n";
    return 1;
  }

  MountState state;
  std::string load_error;
  if (!LoadMountState(state_file, &state, &load_error)) {
    std::cerr << "Error: invalid mount state: " << load_error << "\n";
    return 1;
  }
  if (state.telemetry_jsonl_path.empty()) {
    std::cerr << "Error: telemetry JSONL path is not configured for mount " << mount_point << "\n";
    return 1;
  }

  const auto files = CollectTelemetryFiles(state.telemetry_jsonl_path);
  for (auto it = files.rbegin(); it != files.rend(); ++it) {
    std::ifstream in(*it, std::ios::binary);
    if (!in) {
      continue;
    }
    std::string line;
    while (std::getline(in, line)) {
      std::cout << line << "\n";
    }
  }
  return 0;
}

int CmdTelemetryDumpMountedFiltered(std::string mount_point, const TelemetryDumpOptions& opt) {
  mount_point = NormalizeMountPoint(std::move(mount_point));
  if (!IsValidMountPoint(mount_point)) {
    std::cerr << "Error: invalid mount point, expected format X:\n";
    return 1;
  }

  const auto state_file = MountStateFile(mount_point);
  if (!std::filesystem::exists(state_file)) {
    std::cerr << "Error: mount point is not mounted: " << mount_point << "\n";
    return 1;
  }

  MountState state;
  std::string load_error;
  if (!LoadMountState(state_file, &state, &load_error)) {
    std::cerr << "Error: invalid mount state: " << load_error << "\n";
    return 1;
  }
  if (state.telemetry_jsonl_path.empty()) {
    std::cerr << "Error: telemetry JSONL path is not configured for mount " << mount_point << "\n";
    return 1;
  }

  std::vector<std::string> matched;
  const auto files = CollectTelemetryFiles(state.telemetry_jsonl_path);
  for (auto it = files.rbegin(); it != files.rend(); ++it) {
    std::ifstream in(*it, std::ios::binary);
    if (!in) {
      continue;
    }
    std::string line;
    while (std::getline(in, line)) {
      if (TelemetryLineMatchesFilter(line, opt)) {
        matched.push_back(line);
      }
    }
  }

  std::size_t start = 0;
  if (opt.tail > 0 && matched.size() > opt.tail) {
    start = matched.size() - opt.tail;
  }

  std::vector<std::string> sliced;
  for (std::size_t i = start; i < matched.size(); ++i) {
    sliced.push_back(matched[i]);
  }

  std::size_t page_start = opt.offset;
  if (page_start > sliced.size()) {
    page_start = sliced.size();
  }
  std::size_t page_end = sliced.size();
  if (opt.limit > 0 && page_start + opt.limit < page_end) {
    page_end = page_start + opt.limit;
  }

  std::map<std::string, std::uint64_t> total_by_event;
  std::map<std::string, std::uint64_t> success_by_event;
  std::uint64_t total_success = 0;
  for (const auto& line : sliced) {
    const std::string name = ExtractJsonStringField(line, "name");
    const int success = ExtractJsonBoolField(line, "success");
    if (name.empty() || success == -1) {
      continue;
    }
    ++total_by_event[name];
    if (success == 1) {
      ++total_success;
      ++success_by_event[name];
    }
  }

  if (opt.as_json) {
    std::cout << "{\n";
    std::cout << "  \"schema_version\": \"telemetry-query.v1\",\n";
    std::cout << "  \"explain_schema_version\": \"telemetry-explain.v1\",\n";
    std::cout << "  \"explain_schema_policy\": {\n";
    std::cout << "    \"versioning\": \"semver-compatible\",\n";
    std::cout << "    \"major_breaking_changes\": true,\n";
    std::cout << "    \"minor_additive_changes\": true\n";
    std::cout << "  },\n";
    std::cout << "  \"schema_policy\": {\n";
    std::cout << "    \"versioning\": \"semver-compatible\",\n";
    std::cout << "    \"major_breaking_changes\": true,\n";
    std::cout << "    \"minor_additive_changes\": true\n";
    std::cout << "  },\n";
    std::cout << "  \"mount\": \"" << EscapeJson(mount_point) << "\",\n";
    std::cout << "  \"files_scanned\": " << files.size() << ",\n";
      const std::size_t scanned_entries = matched.size();
      std::cout << "  \"total_matched\": " << sliced.size() << ",\n";
      std::cout << "  \"total_success\": " << total_success << ",\n";
    std::cout << "  \"query\": {\n";
    std::cout << "    \"selector_mode\": \""
              << (opt.selector_mode == TelemetryDumpOptions::SelectorMode::kAll ? "all" : "any")
              << "\",\n";
    std::cout << "    \"event\": [";
    for (std::size_t i = 0; i < opt.include_events.size(); ++i) {
      if (i != 0) {
        std::cout << ", ";
      }
      std::cout << "\"" << EscapeJson(opt.include_events[i]) << "\"";
    }
    std::cout << "],\n";
    std::cout << "    \"exclude_event\": [";
    for (std::size_t i = 0; i < opt.exclude_events.size(); ++i) {
      if (i != 0) {
        std::cout << ", ";
      }
      std::cout << "\"" << EscapeJson(opt.exclude_events[i]) << "\"";
    }
    std::cout << "],\n";
    std::cout << "    \"event_prefix\": \"" << EscapeJson(opt.event_prefix) << "\",\n";
    std::cout << "    \"event_contains\": \"" << EscapeJson(opt.event_contains) << "\",\n";
    std::cout << "    \"where\": \""
              << EscapeJson(opt.where_enabled ? opt.where_expression_normalized : std::string()) << "\",\n";
    if (opt.success_filter == -1) {
      std::cout << "    \"success\": \"any\",\n";
    } else {
      std::cout << "    \"success\": \"" << (opt.success_filter == 1 ? "true" : "false") << "\",\n";
    }
    std::cout << "    \"tail\": " << opt.tail << "\n";
    std::cout << "  },\n";
    if (opt.explain) {
      std::size_t rpn_predicates = 0;
      std::size_t rpn_not = 0;
      std::size_t rpn_and = 0;
      std::size_t rpn_or = 0;
      std::size_t pred_event = 0;
      std::size_t pred_detail = 0;
      std::size_t pred_success = 0;
      for (const auto& token : opt.where_compiled.rpn) {
        if (token.kind == TelemetryWhereExpression::CompiledToken::Kind::kPredicate) {
          ++rpn_predicates;
          switch (token.predicate.kind) {
            case TelemetryWherePredicate::Kind::kEventEquals:
            case TelemetryWherePredicate::Kind::kEventIEquals:
            case TelemetryWherePredicate::Kind::kEventIContains:
            case TelemetryWherePredicate::Kind::kEventStartsWith:
            case TelemetryWherePredicate::Kind::kEventEndsWith:
            case TelemetryWherePredicate::Kind::kEventPrefix:
            case TelemetryWherePredicate::Kind::kEventSuffix:
            case TelemetryWherePredicate::Kind::kEventContains:
              ++pred_event;
              break;
            case TelemetryWherePredicate::Kind::kDetailPrefix:
            case TelemetryWherePredicate::Kind::kDetailSuffix:
            case TelemetryWherePredicate::Kind::kDetailContains:
            case TelemetryWherePredicate::Kind::kDetailIContains:
              ++pred_detail;
              break;
            case TelemetryWherePredicate::Kind::kSuccessEquals:
              ++pred_success;
              break;
          }
        } else if (token.kind == TelemetryWhereExpression::CompiledToken::Kind::kNot) {
          ++rpn_not;
        } else if (token.kind == TelemetryWhereExpression::CompiledToken::Kind::kAnd) {
          ++rpn_and;
        } else {
          ++rpn_or;
        }
      }

      std::cout << "  \"query_plan\": {\n";
      std::cout << "    \"where_enabled\": " << (opt.where_enabled ? "true" : "false") << ",\n";
      std::cout << "    \"selector_mode\": \""
                << (opt.selector_mode == TelemetryDumpOptions::SelectorMode::kAll ? "all" : "any")
                << "\",\n";
      std::cout << "    \"positive_selector_count\": "
                << (opt.include_events.size() + (opt.event_prefix.empty() ? 0 : 1) +
                    (opt.event_contains.empty() ? 0 : 1))
                << ",\n";
      std::cout << "    \"negative_selector_count\": " << opt.exclude_events.size() << ",\n";
      std::cout << "    \"scanned_entries\": " << scanned_entries << ",\n";
      std::cout << "    \"matched_entries\": " << sliced.size() << ",\n";
      std::cout << "    \"scan_match_ratio\": "
                << (scanned_entries == 0 ? 0.0
                                         : static_cast<double>(sliced.size()) /
                                               static_cast<double>(scanned_entries))
                << ",\n";
      std::cout << "    \"where_rpn_tokens\": " << opt.where_compiled.rpn.size() << ",\n";
      std::cout << "    \"normalized_where_hash\": "
                << StableWhereHash(opt.where_expression_normalized) << ",\n";
      std::cout << "    \"where_feature_mask\": " << ComputeWhereFeatureMask(opt.where_compiled)
                << ",\n";
      std::cout << "    \"rpn_predicates\": " << rpn_predicates << ",\n";
      std::cout << "    \"rpn_not\": " << rpn_not << ",\n";
      std::cout << "    \"rpn_and\": " << rpn_and << ",\n";
      std::cout << "    \"rpn_or\": " << rpn_or << ",\n";
      std::cout << "    \"pred_event\": " << pred_event << ",\n";
      std::cout << "    \"pred_detail\": " << pred_detail << ",\n";
      std::cout << "    \"pred_success\": " << pred_success << "\n";
      std::cout << "  },\n";
      std::cout << "  \"query_plan_order\": ["
                << "\"where_enabled\", \"selector_mode\", \"positive_selector_count\", "
                << "\"negative_selector_count\", \"scanned_entries\", \"matched_entries\", "
                << "\"scan_match_ratio\", \"where_rpn_tokens\", \"normalized_where_hash\", "
                << "\"where_feature_mask\", \"rpn_predicates\", \"rpn_not\", \"rpn_and\", "
                << "\"rpn_or\", \"pred_event\", \"pred_detail\", \"pred_success\"],\n";
      const std::size_t planner_score = (rpn_predicates * 2) + (rpn_not * 3) + (rpn_and * 4) + (rpn_or * 5);
      std::string complexity = "simple";
      if (opt.where_compiled.rpn.size() >= 12 || rpn_or >= 3 || rpn_and >= 4) {
        complexity = "complex";
      } else if (opt.where_compiled.rpn.size() >= 6 || rpn_or >= 1 || rpn_and >= 2 || rpn_not >= 1) {
        complexity = "moderate";
      }
      std::cout << "  \"query_plan_score\": " << planner_score << ",\n";
      const double confidence = planner_score == 0
                                    ? 1.0
                                    : std::max(0.1, 1.0 - (static_cast<double>(planner_score) / 100.0));
      std::cout << "  \"query_plan_confidence\": " << confidence << ",\n";
      std::string profile = "focused";
      if (rpn_or >= 2 || pred_detail >= 3) {
        profile = "exploratory";
      }
      if (confidence < 0.5 || planner_score > 18) {
        profile = "expensive";
      }
      std::cout << "  \"query_plan_profile\": \"" << profile << "\",\n";
      std::cout << "  \"query_plan_warnings\": [";
      bool first_warning = true;
      if (confidence < 0.6) {
        std::cout << "\"low_confidence\"";
        first_warning = false;
      }
      if (rpn_or >= 3) {
        if (!first_warning) {
          std::cout << ", ";
        }
        std::cout << "\"high_disjunction\"";
        first_warning = false;
      }
      if (pred_detail > pred_event) {
        if (!first_warning) {
          std::cout << ", ";
        }
        std::cout << "\"detail_heavy_filter\"";
      }
      std::cout << "],\n";
      std::cout << "  \"query_plan_complexity\": \"" << complexity << "\",\n";
      std::cout << "  \"query_plan_flags\": {\n";
      std::cout << "    \"has_negation\": " << (rpn_not > 0 ? "true" : "false") << ",\n";
      std::cout << "    \"has_disjunction\": " << (rpn_or > 0 ? "true" : "false") << ",\n";
      std::cout << "    \"has_detail_predicates\": " << (pred_detail > 0 ? "true" : "false") << "\n";
      std::cout << "  },\n";
      std::cout << "  \"query_plan_rule_ids\": [";
      bool first_rule = true;
      std::size_t rule_count = 0;
      auto emit_rule = [&](const std::string& rule) {
        if (!first_rule) {
          std::cout << ", ";
        }
        first_rule = false;
        ++rule_count;
        std::cout << "\"" << rule << "\"";
      };
      emit_rule("R_BASE_FILTER");
      if (opt.where_enabled) {
        emit_rule("R_WHERE_COMPILED");
      }
      if (rpn_not > 0) {
        emit_rule("R_NOT_OPERATOR");
      }
      if (rpn_or > 0) {
        emit_rule("R_OR_BRANCH");
      }
      if (pred_detail > 0) {
        emit_rule("R_DETAIL_PREDICATES");
      }
      if (confidence < 0.6) {
        emit_rule("R_LOW_CONFIDENCE");
      }
      std::cout << "],\n";
      std::cout << "  \"query_plan_rule_count\": " << rule_count << ",\n";
      const std::uint64_t trace_id_seed = StableWhereHash(opt.where_expression_normalized) ^
                                          static_cast<std::uint64_t>(opt.where_compiled.rpn.size() * 131);
      std::cout << "  \"query_plan_trace_id\": \"trace-" << trace_id_seed << "\",\n";
      const std::size_t phase_parse_ms = 1 + (opt.where_compiled.rpn.size() / 4);
      const std::size_t phase_filter_ms = 1 + (scanned_entries / 64);
      const std::size_t phase_aggregate_ms = 1 + (total_by_event.size() / 8);
      std::cout << "  \"query_plan_phase_ms\": {\n";
      std::cout << "    \"parse\": " << phase_parse_ms << ",\n";
      std::cout << "    \"filter\": " << phase_filter_ms << ",\n";
      std::cout << "    \"aggregate\": " << phase_aggregate_ms << "\n";
      std::cout << "  },\n";
      std::cout << "  \"query_plan_phase_budget_ms\": {\n";
      std::cout << "    \"parse\": 5,\n";
      std::cout << "    \"filter\": 12,\n";
      std::cout << "    \"aggregate\": 8\n";
      std::cout << "  },\n";
      std::cout << "  \"query_plan_phase_status\": {\n";
      std::cout << "    \"parse\": \"" << (phase_parse_ms <= 5 ? "ok" : "over_budget") << "\",\n";
      std::cout << "    \"filter\": \"" << (phase_filter_ms <= 12 ? "ok" : "over_budget") << "\",\n";
      std::cout << "    \"aggregate\": \"" << (phase_aggregate_ms <= 8 ? "ok" : "over_budget") << "\"\n";
      std::cout << "  },\n";
      std::cout << "  \"query_plan_stage_count\": 3,\n";
      const std::size_t positive_selector_count =
          opt.include_events.size() + (opt.event_prefix.empty() ? 0 : 1) + (opt.event_contains.empty() ? 0 : 1);
      const double selector_density = scanned_entries == 0
                                          ? 0.0
                                          : static_cast<double>(positive_selector_count) /
                                                static_cast<double>(scanned_entries);
      std::cout << "  \"query_plan_selector_density\": " << selector_density << ",\n";
      const std::size_t logical_ops = rpn_and + rpn_or;
      const double operator_balance = logical_ops == 0
                                          ? 0.0
                                          : static_cast<double>(rpn_or) / static_cast<double>(logical_ops);
      std::cout << "  \"query_plan_operator_balance\": " << operator_balance << ",\n";
      std::ostringstream signature_stream;
      signature_stream << "p" << rpn_predicates << "-n" << rpn_not << "-a" << rpn_and
                       << "-o" << rpn_or << "-d" << pred_detail << "-s" << pred_success;
      std::cout << "  \"query_plan_signature\": \"" << signature_stream.str() << "\",\n";
    }
    std::cout << "  \"offset\": " << page_start << ",\n";
    std::cout << "  \"limit\": " << opt.limit << ",\n";
    std::cout << "  \"entries\": [";
    for (std::size_t i = page_start; i < page_end; ++i) {
      if (i != page_start) {
        std::cout << ", ";
      }
      std::cout << "\"" << EscapeJson(sliced[i]) << "\"";
    }
    std::cout << "],\n";
    std::cout << "  \"stats\": {\n";
    std::cout << "    \"events\": [";
    bool first = true;
    for (const auto& kv : total_by_event) {
      if (!first) {
        std::cout << ", ";
      }
      first = false;
      const std::uint64_t success_count =
          success_by_event.count(kv.first) > 0 ? success_by_event[kv.first] : 0;
      const double success_rate = kv.second == 0 ? 0.0
                                                 : static_cast<double>(success_count) /
                                                       static_cast<double>(kv.second);
      std::cout << "{\"name\":\"" << EscapeJson(kv.first) << "\"," << "\"count\":" << kv.second
                << "," << "\"success_count\":" << success_count << "," << "\"success_rate\":"
                << success_rate << "}";
    }
    std::cout << "]\n";
    std::cout << "  }\n";
    std::cout << "}\n";
    return 0;
  }

  for (std::size_t i = page_start; i < page_end; ++i) {
    std::cout << sliced[i] << "\n";
  }
  return 0;
}

int CmdTelemetryClearMounted(std::string mount_point) {
  mount_point = NormalizeMountPoint(std::move(mount_point));
  if (!IsValidMountPoint(mount_point)) {
    std::cerr << "Error: invalid mount point, expected format X:\n";
    return 1;
  }

  const auto state_file = MountStateFile(mount_point);
  if (!std::filesystem::exists(state_file)) {
    std::cerr << "Error: mount point is not mounted: " << mount_point << "\n";
    return 1;
  }

  MountState state;
  std::string load_error;
  if (!LoadMountState(state_file, &state, &load_error)) {
    std::cerr << "Error: invalid mount state: " << load_error << "\n";
    return 1;
  }
  if (state.telemetry_jsonl_path.empty()) {
    std::cerr << "Error: telemetry JSONL path is not configured for mount " << mount_point << "\n";
    return 1;
  }

  std::ofstream out(state.telemetry_jsonl_path, std::ios::binary | std::ios::trunc);
  if (!out) {
    std::cerr << "Error: cannot clear telemetry JSONL file: " << state.telemetry_jsonl_path << "\n";
    return 1;
  }
  std::cout << "Cleared telemetry JSONL for " << mount_point << "\n";
  return 0;
}

int CmdTelemetryListMounted(std::string mount_point) {
  mount_point = NormalizeMountPoint(std::move(mount_point));
  if (!IsValidMountPoint(mount_point)) {
    std::cerr << "Error: invalid mount point, expected format X:\n";
    return 1;
  }

  const auto state_file = MountStateFile(mount_point);
  if (!std::filesystem::exists(state_file)) {
    std::cerr << "Error: mount point is not mounted: " << mount_point << "\n";
    return 1;
  }

  MountState state;
  std::string load_error;
  if (!LoadMountState(state_file, &state, &load_error)) {
    std::cerr << "Error: invalid mount state: " << load_error << "\n";
    return 1;
  }
  if (state.telemetry_jsonl_path.empty()) {
    std::cerr << "Error: telemetry JSONL path is not configured for mount " << mount_point << "\n";
    return 1;
  }

  const std::filesystem::path base = state.telemetry_jsonl_path;
  const auto files = CollectTelemetryFiles(base);

  std::cout << "Telemetry files for " << mount_point << "\n";
  for (const auto& f : files) {
    std::error_code ec;
    const auto sz = std::filesystem::file_size(f, ec);
    if (ec) {
      std::cout << f.string() << " size=unknown\n";
    } else {
      std::cout << f.string() << " size=" << sz << "\n";
    }
  }
  if (files.empty()) {
    std::cout << "(no telemetry files found)\n";
  }
  return 0;
}

int CmdTelemetryStatsMounted(std::string mount_point, bool as_json) {
  mount_point = NormalizeMountPoint(std::move(mount_point));
  if (!IsValidMountPoint(mount_point)) {
    std::cerr << "Error: invalid mount point, expected format X:\n";
    return 1;
  }

  const auto state_file = MountStateFile(mount_point);
  if (!std::filesystem::exists(state_file)) {
    std::cerr << "Error: mount point is not mounted: " << mount_point << "\n";
    return 1;
  }

  MountState state;
  std::string load_error;
  if (!LoadMountState(state_file, &state, &load_error)) {
    std::cerr << "Error: invalid mount state: " << load_error << "\n";
    return 1;
  }
  if (state.telemetry_jsonl_path.empty()) {
    std::cerr << "Error: telemetry JSONL path is not configured for mount " << mount_point << "\n";
    return 1;
  }

  const auto files = CollectTelemetryFiles(state.telemetry_jsonl_path);
  std::map<std::string, std::uint64_t> total_by_event;
  std::map<std::string, std::uint64_t> success_by_event;
  std::uint64_t total_events = 0;
  std::uint64_t total_success = 0;

  for (auto it = files.rbegin(); it != files.rend(); ++it) {
    std::ifstream in(*it, std::ios::binary);
    if (!in) {
      continue;
    }
    std::string line;
    while (std::getline(in, line)) {
      const std::string name = ExtractJsonStringField(line, "name");
      const int success = ExtractJsonBoolField(line, "success");
      if (name.empty() || success == -1) {
        continue;
      }
      ++total_events;
      ++total_by_event[name];
      if (success == 1) {
        ++total_success;
        ++success_by_event[name];
      }
    }
  }

  if (as_json) {
    std::cout << "{\n";
    std::cout << "  \"mount\": \"" << EscapeJson(mount_point) << "\",\n";
    std::cout << "  \"total_events\": " << total_events << ",\n";
    std::cout << "  \"total_success\": " << total_success << ",\n";
    std::cout << "  \"events\": [";
    bool first = true;
    for (const auto& kv : total_by_event) {
      if (!first) {
        std::cout << ", ";
      }
      first = false;
      const std::uint64_t success_count =
          success_by_event.count(kv.first) > 0 ? success_by_event[kv.first] : 0;
      const double success_rate = kv.second == 0 ? 0.0 :
          static_cast<double>(success_count) / static_cast<double>(kv.second);
      std::cout << "{\"name\":\"" << EscapeJson(kv.first) << "\","
                << "\"count\":" << kv.second << ","
                << "\"success_count\":" << success_count << ","
                << "\"success_rate\":" << success_rate << "}";
    }
    std::cout << "]\n";
    std::cout << "}\n";
    return 0;
  }

  std::cout << "Telemetry stats for " << mount_point << "\n";
  std::cout << "Total events: " << total_events << "\n";
  std::cout << "Total success: " << total_success << "\n";
  for (const auto& kv : total_by_event) {
    const std::uint64_t success_count =
        success_by_event.count(kv.first) > 0 ? success_by_event[kv.first] : 0;
    const double success_rate = kv.second == 0 ? 0.0 :
        static_cast<double>(success_count) / static_cast<double>(kv.second);
    std::cout << kv.first << " count=" << kv.second << " success=" << success_count
              << " success_rate=" << success_rate << "\n";
  }

  return 0;
}

int CmdWriteAdd(const std::string& image_path, const std::string& host_file, const std::string& windows_name) {
  D64ImageEditor editor;
  if (!editor.Open(image_path)) {
    std::cerr << "Error: " << editor.LastError() << "\n";
    return 1;
  }
  if (!editor.AddFile(host_file, windows_name)) {
    std::cerr << "Error: " << editor.LastError() << "\n";
    return 1;
  }
  std::cout << "Added " << windows_name << "\n";
  return 0;
}

int CmdWriteDel(const std::string& image_path, const std::string& windows_name) {
  D64ImageEditor editor;
  if (!editor.Open(image_path)) {
    std::cerr << "Error: " << editor.LastError() << "\n";
    return 1;
  }
  if (!editor.DeleteFile(windows_name)) {
    std::cerr << "Error: " << editor.LastError() << "\n";
    return 1;
  }
  std::cout << "Deleted " << windows_name << "\n";
  return 0;
}

int CmdWriteRen(const std::string& image_path,
                const std::string& old_windows_name,
                const std::string& new_windows_name) {
  D64ImageEditor editor;
  if (!editor.Open(image_path)) {
    std::cerr << "Error: " << editor.LastError() << "\n";
    return 1;
  }
  if (!editor.RenameFile(old_windows_name, new_windows_name)) {
    std::cerr << "Error: " << editor.LastError() << "\n";
    return 1;
  }
  std::cout << "Renamed " << old_windows_name << " -> " << new_windows_name << "\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    PrintUsage();
    return 1;
  }

  const std::string command = argv[1];

  if (command == "version") {
    return CmdVersion();
  }

  if (command == "unmount") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    return CmdUnmount(argv[2]);
  }

  if (command == "mounts") {
    return CmdMounts();
  }

  if (command == "dir-mounted") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    return CmdDirMounted(argv[2]);
  }

  if (command == "read-mounted") {
    if (argc < 4) {
      PrintUsage();
      return 1;
    }
    return CmdReadMounted(argv[2], argv[3]);
  }

  if (command == "volume-mounted") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    return CmdVolumeMounted(argv[2]);
  }

  if (command == "stats-mounted") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    return CmdStatsMounted(argv[2]);
  }

  if (command == "check-mounted") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    return CmdCheckMounted(argv[2]);
  }

  if (command == "backend-diag-mounted") {
    if (argc != 3 && argc != 4) {
      PrintUsage();
      return 1;
    }
    bool as_json = false;
    if (argc == 4) {
      if (std::string(argv[3]) != "--json") {
        PrintUsage();
        return 1;
      }
      as_json = true;
    }
    return CmdBackendDiagMounted(argv[2], as_json);
  }

  if (command == "telemetry-dump-mounted") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    TelemetryDumpOptions opt;
    for (int i = 3; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--event") {
        if (i + 1 >= argc) {
          PrintUsage();
          return 1;
        }
        opt.include_events.push_back(argv[++i]);
        continue;
      }
      if (arg == "--exclude-event") {
        if (i + 1 >= argc) {
          PrintUsage();
          return 1;
        }
        opt.exclude_events.push_back(argv[++i]);
        continue;
      }
      if (arg == "--event-prefix") {
        if (i + 1 >= argc) {
          PrintUsage();
          return 1;
        }
        opt.event_prefix = argv[++i];
        continue;
      }
      if (arg == "--event-contains") {
        if (i + 1 >= argc) {
          PrintUsage();
          return 1;
        }
        opt.event_contains = argv[++i];
        continue;
      }
      if (arg == "--selector-mode") {
        if (i + 1 >= argc) {
          PrintUsage();
          return 1;
        }
        const std::string value = argv[++i];
        if (value == "all") {
          opt.selector_mode = TelemetryDumpOptions::SelectorMode::kAll;
        } else if (value == "any") {
          opt.selector_mode = TelemetryDumpOptions::SelectorMode::kAny;
        } else {
          PrintUsage();
          return 1;
        }
        continue;
      }
      if (arg == "--where") {
        if (i + 1 >= argc) {
          PrintUsage();
          return 1;
        }
        opt.where_expression = argv[++i];
        opt.where_enabled = true;
        continue;
      }
      if (arg == "--success") {
        if (i + 1 >= argc) {
          PrintUsage();
          return 1;
        }
        const std::string value = argv[++i];
        if (value == "true") {
          opt.success_filter = 1;
        } else if (value == "false") {
          opt.success_filter = 0;
        } else {
          PrintUsage();
          return 1;
        }
        continue;
      }
      if (arg == "--tail") {
        if (i + 1 >= argc) {
          PrintUsage();
          return 1;
        }
        try {
          opt.tail = static_cast<std::size_t>(std::stoul(argv[++i]));
        } catch (...) {
          PrintUsage();
          return 1;
        }
        continue;
      }
      if (arg == "--offset") {
        if (i + 1 >= argc) {
          PrintUsage();
          return 1;
        }
        try {
          opt.offset = static_cast<std::size_t>(std::stoul(argv[++i]));
        } catch (...) {
          PrintUsage();
          return 1;
        }
        continue;
      }
      if (arg == "--limit") {
        if (i + 1 >= argc) {
          PrintUsage();
          return 1;
        }
        try {
          opt.limit = static_cast<std::size_t>(std::stoul(argv[++i]));
        } catch (...) {
          PrintUsage();
          return 1;
        }
        continue;
      }
      if (arg == "--json") {
        opt.as_json = true;
        continue;
      }
      if (arg == "--bundle") {
        opt.bundle = true;
        opt.as_json = true;
        continue;
      }
      if (arg == "--explain") {
        opt.explain = true;
        opt.as_json = true;
        continue;
      }
      PrintUsage();
      return 1;
    }
    std::string normalize_error;
    if (!NormalizeTelemetryDumpOptions(&opt, &normalize_error)) {
      std::cerr << "Error: invalid telemetry query options: " << normalize_error << "\n";
      return 1;
    }

    if (!opt.where_enabled && !HasPositiveSelectors(opt) && opt.exclude_events.empty() && opt.success_filter == -1 &&
        opt.tail == 0 &&
        opt.offset == 0 && opt.limit == 0 && !opt.as_json && !opt.bundle) {
      return CmdTelemetryDumpMounted(argv[2]);
    }
    return CmdTelemetryDumpMountedFiltered(argv[2], opt);
  }

  if (command == "telemetry-clear-mounted") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    return CmdTelemetryClearMounted(argv[2]);
  }

  if (command == "telemetry-list-mounted") {
    if (argc < 3) {
      PrintUsage();
      return 1;
    }
    return CmdTelemetryListMounted(argv[2]);
  }

  if (command == "telemetry-stats-mounted") {
    if (argc != 3 && argc != 4) {
      PrintUsage();
      return 1;
    }
    bool as_json = false;
    if (argc == 4) {
      if (std::string(argv[3]) != "--json") {
        PrintUsage();
        return 1;
      }
      as_json = true;
    }
    return CmdTelemetryStatsMounted(argv[2], as_json);
  }

  if (command == "write-add") {
    if (argc < 5) {
      PrintUsage();
      return 1;
    }
    return CmdWriteAdd(argv[2], argv[3], argv[4]);
  }

  if (command == "write-del") {
    if (argc < 4) {
      PrintUsage();
      return 1;
    }
    return CmdWriteDel(argv[2], argv[3]);
  }

  if (command == "write-ren") {
    if (argc < 5) {
      PrintUsage();
      return 1;
    }
    return CmdWriteRen(argv[2], argv[3], argv[4]);
  }

  if (command == "winfsp-preflight") {
    if (argc < 4) {
      PrintUsage();
      return 1;
    }
    return CmdWinfspPreflight(argv[2], argv[3]);
  }

  if (argc < 3) {
    PrintUsage();
    return 1;
  }

  const std::string image_path = argv[2];

  if (command == "info") {
    return CmdInfo(image_path);
  }

  if (command == "ls") {
    return CmdLs(image_path);
  }

  if (command == "extract") {
    const std::string output_dir = argc >= 4 ? argv[3] : "";
    return CmdExtract(image_path, output_dir);
  }

  if (command == "mount") {
    if (argc != 4 && argc != 6) {
      PrintUsage();
      return 1;
    }

    std::string backend_name = "winfsp";
    if (argc == 6) {
      if (std::string(argv[4]) != "--backend") {
        PrintUsage();
        return 1;
      }
      backend_name = argv[5];
    }

    return CmdMountWithBackend(image_path, argv[3], backend_name);
  }

  if (command == "backend-diag") {
    if (argc < 3 || argc > 6) {
      PrintUsage();
      return 1;
    }

    std::string backend_name = "winfsp";
    bool as_json = false;
    for (int i = 3; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--json") {
        as_json = true;
        continue;
      }
      if (arg == "--backend") {
        if (i + 1 >= argc) {
          PrintUsage();
          return 1;
        }
        backend_name = argv[i + 1];
        ++i;
        continue;
      }

      PrintUsage();
      return 1;
    }

    return CmdBackendDiag(image_path, backend_name, as_json);
  }

  PrintUsage();
  return 1;
}

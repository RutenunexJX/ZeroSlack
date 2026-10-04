# Keep the pinned Slang checkout pristine. Build a narrowly scoped host-source
# loader extension as an overlay; all Slang consumers see the same header ABI.
# This is deliberately checked against the pinned upstream spelling so an
# upstream upgrade cannot silently drop capture of failed include lookups.
set(_zs_slang_capture_dir "${CMAKE_CURRENT_BINARY_DIR}/slang-source-capture")
set(_zs_slang_header "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/slang/include/slang/text/SourceManager.h")
set(_zs_slang_source "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/slang/source/text/SourceManager.cpp")
file(READ "${_zs_slang_header}" _zs_header)
file(READ "${_zs_slang_source}" _zs_source)
set(_zs_loader_anchor "    bool disableLocalIncludes = false;")
set(_zs_read_anchor "if (std::error_code ec = OS::readFile(absPath, buffer))")
string(FIND "${_zs_header}" "${_zs_loader_anchor}" _zs_header_anchor)
string(FIND "${_zs_source}" "${_zs_read_anchor}" _zs_source_anchor)
if(_zs_header_anchor EQUAL -1 OR _zs_source_anchor EQUAL -1)
    message(FATAL_ERROR "Pinned Slang SourceManager changed; review the ZeroSlack source capture overlay")
endif()
string(REPLACE "#include <filesystem>" "#include <filesystem>\n#include <functional>" _zs_header "${_zs_header}")
string(REPLACE "${_zs_loader_anchor}" [=[    bool disableLocalIncludes = false;
public:
    // Install before reading. Concurrent SourceManager reads require a safe
    // callback; the ZeroSlack compilation context owns this on one worker.
    using SourceLoader = std::function<std::error_code(
        const std::filesystem::path&, SmallVector<char>&)>;
    void setSourceLoader(SourceLoader loader) { sourceLoader = std::move(loader); }
private:
    SourceLoader sourceLoader;]=] _zs_header "${_zs_header}")
string(REPLACE "${_zs_read_anchor}"
    "if (std::error_code ec = sourceLoader ? sourceLoader(absPath, buffer) : OS::readFile(absPath, buffer))"
    _zs_source "${_zs_source}")
file(MAKE_DIRECTORY "${_zs_slang_capture_dir}/include/slang/text")
file(CONFIGURE OUTPUT "${_zs_slang_capture_dir}/include/slang/text/SourceManager.h"
    CONTENT "${_zs_header}" @ONLY)
file(CONFIGURE OUTPUT "${_zs_slang_capture_dir}/SourceManager.cpp"
    CONTENT "${_zs_source}" @ONLY)
set_source_files_properties("${_zs_slang_source}" TARGET_DIRECTORY slang_slang
    PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(slang_slang PRIVATE "${_zs_slang_capture_dir}/SourceManager.cpp")
target_include_directories(slang_slang BEFORE PUBLIC
    "$<BUILD_INTERFACE:${_zs_slang_capture_dir}/include>")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_zs_slang_header}" "${_zs_slang_source}")

# Cooperative checkpoints in the pinned parser / lazy elaborator. The callback
# is scoped to one owned compilation thread; no global cancellation state or
# Slang public-object ABI changes are needed. Throwing discards the complete
# in-progress compilation through normal RAII, never a reusable partial AST.
file(CONFIGURE OUTPUT "${_zs_slang_capture_dir}/include/slang/ZeroSlackCancellation.h" CONTENT [=[
#pragma once
#include <functional>
#include <cstdint>
namespace slang::zeroslack {
struct CompilationCancelled {};
inline thread_local const std::function<bool()>* cancellation = nullptr;
inline thread_local std::uint32_t checkpointCount = 0;
inline void checkpoint() {
    if (cancellation && (++checkpointCount % 128u == 0u) && (*cancellation)())
        throw CompilationCancelled{};
}
class CancellationScope {
    const std::function<bool()>* previous;
public:
    explicit CancellationScope(const std::function<bool()>& callback)
        : previous(cancellation) { cancellation = callback ? &callback : nullptr; }
    ~CancellationScope() { cancellation = previous; }
    CancellationScope(const CancellationScope&) = delete;
};
}
]=] @ONLY)
function(zs_slang_cancellation_overlay relative_path)
    set(original "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/slang/source/${relative_path}")
    file(READ "${original}" source)
    foreach(anchor IN LISTS ARGN)
        string(FIND "${source}" "${anchor}" found)
        if(found EQUAL -1)
            message(FATAL_ERROR "Pinned Slang cancellation checkpoint changed: ${relative_path}: ${anchor}")
        endif()
        string(REPLACE "${anchor}" "${anchor}\n    slang::zeroslack::checkpoint();" source "${source}")
    endforeach()
    set(source "#include <slang/ZeroSlackCancellation.h>\n${source}")
    get_filename_component(name "${relative_path}" NAME)
    file(CONFIGURE OUTPUT "${_zs_slang_capture_dir}/cancel-${name}" CONTENT "${source}" @ONLY)
    set_source_files_properties("${original}" TARGET_DIRECTORY slang_slang PROPERTIES HEADER_FILE_ONLY TRUE)
    target_sources(slang_slang PRIVATE "${_zs_slang_capture_dir}/cancel-${name}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${original}")
endfunction()
zs_slang_cancellation_overlay(parsing/Lexer.cpp "Token Lexer::lex(KeywordVersion keywordVersion) {")
zs_slang_cancellation_overlay(parsing/Preprocessor.cpp "Token Preprocessor::nextRaw() {")
zs_slang_cancellation_overlay(parsing/ParserBase.cpp "Token ParserBase::consume() {")
zs_slang_cancellation_overlay(ast/Scope.cpp "void Scope::addMembers(const SyntaxNode& syntax) {" "void Scope::elaborate() const {")
zs_slang_cancellation_overlay(ast/Expression.cpp "ConstantValue Expression::eval(EvalContext& context) const {")
zs_slang_cancellation_overlay(ast/EvalContext.cpp "bool EvalContext::step(SourceLocation loc) {")
zs_slang_cancellation_overlay(ast/Statement.cpp "ER Statement::eval(EvalContext& context) const {")

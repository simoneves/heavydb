// PR 8 spike: what a material's compile costs when its shared code is an imported
// Slang module, against the same shader with that code spliced in, as today.
//
// Compiles through a compile request with SlangBackend's settings, in two ways:
//   fresh   - a new session per compile, as SlangBackend does today
//   persist - one session for every compile, so that a module it has loaded once is
//             not loaded again
//
// Usage: compile_timing <stage> <iterations> <search path>... -- <shader>...
// where <stage> is vertex, fragment or compute.

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <slang-com-ptr.h>
#include <slang-deprecated.h>
#include <slang.h>

namespace {

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::string read_file(const std::string& path) {
  std::ifstream file(path);
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

struct Compiler {
  Slang::ComPtr<slang::IGlobalSession> global;
  SlangProfileID profile = SLANG_PROFILE_UNKNOWN;
  std::vector<std::string> search_paths;
  SlangStage stage = SLANG_STAGE_NONE;
  std::string last_diagnostics;

  Slang::ComPtr<slang::ISession> createSession() {
    // SlangBackend's settings
    slang::TargetDesc target;
    target.format = SLANG_SPIRV;
    target.profile = profile;
    target.forceGLSLScalarBufferLayout = true;

    static std::vector<slang::CompilerOptionEntry> options = [] {
      bool const report = std::getenv("SPIKE_REPORT_PERF") != nullptr;
      std::vector<slang::CompilerOptionEntry> o(report ? 6 : 5);
      if (report) {
        o[5].name = slang::CompilerOptionName::ReportPerfBenchmark;
        o[5].value.intValue0 = 1;
      }
      o[0].name = slang::CompilerOptionName::EmitSpirvDirectly;
      o[0].value.intValue0 = 1;
      o[1].name = slang::CompilerOptionName::VulkanUseEntryPointName;
      o[1].value.intValue0 = 1;
      o[2].name = slang::CompilerOptionName::DisableWarnings;
      o[2].value.kind = slang::CompilerOptionValueKind::String;
      o[2].value.stringValue0 = "39001";
      o[3].name = slang::CompilerOptionName::Optimization;
      o[3].value.intValue0 = SLANG_OPTIMIZATION_LEVEL_NONE;
      o[4].name = slang::CompilerOptionName::DefaultImageFormatUnknown;
      o[4].value.intValue0 = 1;
      return o;
    }();

    std::vector<const char*> paths;
    for (auto const& path : search_paths) {
      paths.push_back(path.c_str());
    }
    slang::SessionDesc desc;
    desc.targets = &target;
    desc.targetCount = 1;
    desc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_ROW_MAJOR;
    desc.searchPaths = paths.data();
    desc.searchPathCount = static_cast<SlangInt>(paths.size());
    desc.allowGLSLSyntax = true;
    desc.compilerOptionEntries = options.data();
    desc.compilerOptionEntryCount = static_cast<uint32_t>(options.size());

    Slang::ComPtr<slang::ISession> session;
    if (SLANG_FAILED(global->createSession(desc, session.writeRef()))) {
      std::cerr << "createSession failed" << std::endl;
      std::exit(1);
    }
    return session;
  }

  // Compiles one shader, returning its SPIR-V size, or exiting with the diagnostics
  size_t compile(slang::ISession& session, const std::string& name, const std::string& source) {
    Slang::ComPtr<slang::ICompileRequest> request;
    session.createCompileRequest(request.writeRef());
    auto const unit = request->addTranslationUnit(SLANG_SOURCE_LANGUAGE_GLSL, "stage0");
    request->addTranslationUnitSourceString(unit, name.c_str(), source.c_str());
    auto const entry = request->addEntryPoint(unit, "main", stage);
    if (SLANG_FAILED(request->compile())) {
      std::cerr << name << ":\n" << request->getDiagnosticOutput() << std::endl;
      std::exit(1);
    }
    if (std::getenv("SPIKE_REPORT_PERF") && request->getDiagnosticOutput()) {
      last_diagnostics = request->getDiagnosticOutput();
    }
    Slang::ComPtr<ISlangBlob> code;
    request->getEntryPointCodeBlob(entry, 0, code.writeRef());
    return code ? code->getBufferSize() : 0;
  }
};

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 5) {
    std::cerr << "Usage: " << argv[0]
              << " <stage> <iterations> <search path>... -- <shader>..." << std::endl;
    return 1;
  }
  Compiler compiler;
  std::string const stage = argv[1];
  compiler.stage = stage == "vertex"     ? SLANG_STAGE_VERTEX
                   : stage == "fragment" ? SLANG_STAGE_FRAGMENT
                                         : SLANG_STAGE_COMPUTE;
  int const iterations = std::stoi(argv[2]);
  int i = 3;
  for (; i < argc && std::strcmp(argv[i], "--") != 0; ++i) {
    compiler.search_paths.push_back(argv[i]);
  }
  std::vector<std::string> shaders(argv + i + 1, argv + argc);

  auto start = Clock::now();
  SlangGlobalSessionDesc global_desc;
  slang::createGlobalSession(&global_desc, compiler.global.writeRef());
  compiler.global->compileBuiltinModule(slang::BuiltinModuleName::GLSL, 0);
  compiler.profile = compiler.global->findProfile("spirv_1_6");
  std::cout << "global session and GLSL module: " << ms_since(start) << " ms\n";

  for (auto const& path : shaders) {
    auto const source = read_file(path);
    auto const name = path.substr(path.find_last_of('/') + 1);

    // As SlangBackend does today: a session per compile
    double fresh_total = 0;
    size_t bytes = 0;
    for (int n = 0; n < iterations; ++n) {
      start = Clock::now();
      auto session = compiler.createSession();
      bytes = compiler.compile(*session, name, source);
      fresh_total += ms_since(start);
    }

    // One session throughout: the first compile loads any imported module
    auto session = compiler.createSession();
    start = Clock::now();
    compiler.compile(*session, name, source);
    double const persist_first = ms_since(start);
    double persist_total = 0;
    for (int n = 0; n < iterations; ++n) {
      start = Clock::now();
      compiler.compile(*session, name, source);
      persist_total += ms_since(start);
    }

    std::cout << name << " (" << bytes << " bytes of SPIR-V)\n"
              << "  fresh session per compile: " << fresh_total / iterations
              << " ms per compile\n"
              << "  one session: first compile " << persist_first << " ms, then "
              << persist_total / iterations << " ms per compile\n";
    if (!compiler.last_diagnostics.empty()) {
      std::cout << "  last compile's report:\n" << compiler.last_diagnostics;
    }
  }
  // Every shader in one session, as a server keeping its session would: the first
  // loads any module, and the rest find it loaded
  auto shared = compiler.createSession();
  std::cout << "one session shared by all of the above, in order:\n";
  for (auto const& path : shaders) {
    auto const source = read_file(path);
    auto const name = path.substr(path.find_last_of('/') + 1);
    start = Clock::now();
    compiler.compile(*shared, name, source);
    std::cout << "  " << name << ": " << ms_since(start) << " ms\n";
  }
  return 0;
}

#ifndef EASY
#define EASY

#include <easy/runtime/BitcodeTracker.h>
#include <easy/runtime/Context.h>
#include <easy/attributes.h>
#include <easy/param.h>
#include <easy/function_wrapper.h>

#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/Host.h> 
#include <llvm/Target/TargetMachine.h> 
#include <llvm/Support/TargetRegistry.h> 
#include <llvm/Analysis/TargetTransformInfo.h> 
#include <llvm/Analysis/TargetLibraryInfo.h> 
#include <llvm/Support/FileSystem.h>
#include <llvm/Transforms/Utils/Cloning.h>


#include <memory>
#include <type_traits>
#include <tuple>
#include <cassert>

namespace easy {

namespace {
template<class Ret, class ... Params>
FunctionWrapper<Ret(Params ...)>
WrapFunction(std::unique_ptr<Function> F, meta::type_list<Ret, Params ...>) {
  return FunctionWrapper<Ret(Params ...)>(std::move(F));
}

template<class T, class ... Args>
auto jit_with_context(easy::Context const &C, T &&Fun) {

  auto* FunPtr = meta::get_as_pointer(Fun);
  using FunOriginalTy = std::remove_pointer_t<std::decay_t<T>>;

  using new_type_traits = meta::new_function_traits<FunOriginalTy, meta::type_list<Args...>>;
  using new_return_type = typename new_type_traits::return_type;
  using new_parameter_types = typename new_type_traits::parameter_list;

  auto CompiledFunction =
      Function::Compile(reinterpret_cast<void*>(FunPtr), C);

  auto Wrapper =
      WrapFunction(std::move(CompiledFunction),
                   typename new_parameter_types::template push_front<new_return_type> ());
  return Wrapper;
}


template<class T, class ... Args>
easy::Context get_context_for(Args&& ... args) {
  using FunOriginalTy = std::remove_pointer_t<std::decay_t<T>>;
  static_assert(std::is_function<FunOriginalTy>::value,
                "easy::jit: supports only on functions and function pointers");

  using parameter_list = typename meta::function_traits<FunOriginalTy>::parameter_list;

  static_assert(parameter_list::size <= sizeof...(Args),
                "easy::jit: not providing enough argument to actual call");

  easy::Context C;
  easy::set_parameters<parameter_list, Args&&...>(parameter_list(), C,
                                                  std::forward<Args>(args)...);
  return C;
}
}

template<class T, class ... Args>
auto EASY_JIT_COMPILER_INTERFACE jit(T &&Fun, Args&& ... args) {
  auto C = get_context_for<T, Args...>(std::forward<Args>(args)...);
  return jit_with_context<T, Args...>(C, std::forward<T>(Fun));
}

template<class T, class ... Args> std::unique_ptr<llvm::Module> EASY_JIT_COMPILER_INTERFACE easy_jit(T &&Fun, Args&& ... args) 
{
    auto easy_ctx = get_context_for<T, Args...>(std::forward<Args>(args)...);
    auto* func_ptr = meta::get_as_pointer(Fun);
    std::unique_ptr<easy::Function> easy_function = Function::Compile(reinterpret_cast<void*>(func_ptr), easy_ctx);
    llvm::Module const & M = easy_function->getLLVMModule();
    std::unique_ptr<llvm::Module> llmod = llvm::CloneModule(M);
    easy_function.release();

    for (llvm::Function & func: *llmod){
        func.addFnAttr(llvm::Attribute::AlwaysInline);
        func.removeFnAttr(llvm::Attribute::OptimizeNone);
    }

    return llmod;
}

template<typename T, typename... Args>
std::unique_ptr<llvm::Module> EASY_JIT_COMPILER_INTERFACE get_module(llvm::LLVMContext& ctx, T&& fn, Args&&... args)
{
    auto &BT = BitcodeTracker::GetTracker();
    auto* fn_ptr = reinterpret_cast<void*>(meta::get_as_pointer(fn));
    auto mod = BT.getModuleWithContext(fn_ptr, ctx);
    return mod;
}

}

#endif

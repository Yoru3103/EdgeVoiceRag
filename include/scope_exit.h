#pragma once

#include <utility>

// ScopeExit不是一个固定类，它需要一个具体的Function类型，编译器在编译阶段会根据Funtion类型生成具体类
template <typename Function>
class ScopeExit {
public:
    // explicit阻止隐性转换，创建对象应明确调用构造函数不允许编译器随意把一个函数对象隐式转换成 ScopeExit
    // 避免类似 ScopeExit<SomeFunction> guard = function; 的隐式转换
    // std::move本身并不执行移动，只是把对象转换为允许被移动的右值表达式。真正能够移动由Function的移动构造函数决定
    explicit ScopeExit(Function function)
        : function_(std::move(function)) {  // move避免额外复制
    }

    ~ScopeExit() {
        if (active_) {
            function_();
        }
    }

    // auto guard1 = makeScopeExit(cleanup);
    // auto guard2 = guard1;  // 不允许
    ScopeExit(const ScopeExit&) = delete;   // 禁止拷贝构造
    // auto guard1 = makeScopeExit(cleanup1);
    // auto guard2 = makeScopeExit(cleanup2);
    // guard2 = guard1;  // 不允许
    ScopeExit& operator=(const ScopeExit&) = delete;    // 禁止拷贝赋值

    void release() {
        active_ = false;
    }

private:
    Function function_;
    bool active_ = true;
};

template <typename Function>
ScopeExit<Function> makeScopeExit(Function function) {
    return ScopeExit<Function>(std::move(function));
}

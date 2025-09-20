# 从零开始：手把手教你实现一个 C++ 的 `std::variant` (基础数据结构复现实战)

## 前言

你好，C++ 探索者！你是否曾对 `std::variant` 的内部机制感到好奇？<br>
这个 C++17 引入的“类型安全的联合体”，究竟是如何在编译期施展魔法，从而在运行时优雅地存储多种不同类型的值的？

这篇博客就是为你准备的钥匙。我们将一起打开 `variant` 的引擎盖，亲手用 C++ **模板元编程 (Template Metaprogramming)** 的工具，从最基础的零件开始，一步步打造出我们自己的 `Variant`。

本文不是对标准库文档的复述，而是一份详细的构造蓝图和深度的心路历程。

我们将不断提出在实现过程中遇到的棘手问题，然后用 C++ 最精妙的特性给出解决方案，并深入探讨“为什么”是这样设计的。

### 本文包含的技术实践：

- 模板元编程 (TMP)：深入体验在编译期进行计算和类型推导的强大能力。

- 递归模板：学习如何通过递归式模板定义，优雅地处理可变参数模板包 (`Ts...`)。

- `if constexpr` 的威力：见证 C++17 的编译期 `if` 如何简化复杂的模板代码。

- 手动生命周期管理：彻底理解 `placement new` 和显式析构函数调用，掌控对象的诞生与消亡。

- 函数指针表优化：学习一种高级技巧，如何用函数指针数组替代 `switch` 语句，实现高效的类型分发。

- `std::index_sequence`：揭开这个元编程神器的神秘面纱，看它如何辅助我们生成编译期代码。

### 本文不包含的技术内容：

- `std::variant` 的 `visit` 功能实现。

- 异常安全性的深入探讨 (例如 valueless_by_exception)。

读完本文，你将收获的不仅仅是一个可用的 `Variant` 类，更重要的是：

- 元编程思维：学会如何“在编译期思考”，将问题从运行时解决，转移到编译期。

- 设计决策的权衡：理解在实现一个通用组件时，背后隐藏的性能与复杂度的博弈。

- 从底层构建的成就感：亲手实现一个标准库级别的组件，彻底掌握它的每一个细节。

现在，让我们收起对模板的敬畏，正式开始这场激动人心的构建之旅。

---

## 一、项目背景：为什么我们要“造轮子”？ 

联合体 (`union`) 是 C 语言留给我们的一份遗产。

它允许在同一块内存中存储不同类型的数据，以达到节省空间的目的。

但它也是一把臭名昭著的双刃剑：

```cpp
union CStyleUnion
{
    int i;
    float f;
};

CStyleUnion u;
u.i = 10;
float value = u.f; // 灾难！我们用错误的类型去读取数据
```

C-style `union` 没有任何机制来记录当前存储的究竟是哪种类型。这种不安全的操作是无数 bug 的根源。

C++17 的 `std::variant` 完美地解决了这个问题。<br>
它本质上是一个“被增强的”联合体，它额外携带了一个“标签” (discriminator)，在任何时候都明确地知道自己存储的是什么类型。

既然标准库已经有了，我们为什么还要自己造一个呢？

1. **深度学习**：自己动手实现一遍，是理解模板元编程、类型系统和对象生命周期最深刻的方式。
2. **面试加分项**：在求职面试中，能够清晰阐述 `variant` 的实现原理，绝对是你 C++ 内功深厚的最佳证明。
3. **揭开魔法的面纱**：让你在使用 `std::variant` 时，不再把它看作一个黑盒，而是能清晰地预见其性能和行为。

---

## 二、蓝图设计：构建一个类型安全的“魔方”

在动手之前，我们先明确目标。我们要构建一个类 `Variant<Ts...>`，它能做到：

- 存储：在内部的某块内存上，存储 `Ts...` 类型列表中的任意一种。
- 追踪：拥有一个成员变量 (如 `type_idx`)，用于记录当前存储的是第几种类型。
- 构造：能用 `Ts...` 中的任意一种类型的值来构造自己。
- 访问：提供安全的 `get()` 方法，在访问前检查类型，如果类型不匹配则报错。
- 生命周期：正确地管理所存储对象的构造、析构、拷贝和移动。

### 第一步：数据的“收纳盒” - 递归的 `Storage`

我们遇到的第一个问题是：<br>
如何设计一块能容纳 `int`, `std::string`, `double` 等一众大小不一、构造析构行为也各不相同的类型的内存空间？

一个简单的 `union` 不够好，因为它对 **非平凡 (non-trivial)** 的类型（如 `std::string`）有很多限制。

我们可以另辟蹊径，用 **模板递归** 来构建一个“链式”的联合体。

```cpp
namespace variant_utils
{
    template <typename... Ts>
    struct Storage; // 1. 先声明

    // 2. 递归的基准情况：类型列表为空
    template <>
    struct Storage<> {};

    // 3. 递归的通用情况
    template <typename Head, typename... Ts>
    struct Storage<Head, Ts...>
    {
        union
        {
            Head value;            // 要么存储当前类型 Head
            Storage<Ts...> next;   // 要么存储包含剩下类型的 "下一个收纳盒"
        };
    };
}
```

#### 设计细节剖析：

这个设计非常精妙，它像一个“俄罗斯套娃”：

- `Storage<int, std::string, bool>` 是一个联合体，它可以存放一个 `int`，或者一个 `Storage<std::string, bool>`。
- 而 `Storage<std::string, bool>` 又是一个联合体，它可以存放一个 `std::string`，或者一个 `Storage<bool>`。
- `Storage<bool>` 最终可以存放一个 `bool`，或者一个空的 `Storage<>`。

通过这种方式，我们巧妙地将所有类型“串”在了一个递归的联合体结构里。

这块内存的总大小，将由 `Ts...` 中最大的那个类型决定。

### 第二步：编译期的“户口本” - 类型与索引的映射

我们有了“收纳盒” `Storage`，但问题也随之而来：<br>
如果我们想存取 `std::string`（第二个类型），我们怎么知道应该访问 `storage.next.value` 呢？

我们需要在 **编译期** 就建立好类型和它在参数包中位置（索引）的对应关系。这就是模板元编程大显身手的地方。

#### 2.1 按类型查找索引：`find_idx_by_type`

```cpp
namespace variant_utils
{
    // 通用递归模板
    template <int64_t id, typename U, typename T, typename... Ts>
    struct Position
    {
        // 如果没找到，就去剩下的 Ts... 里继续找，id + 1
        constexpr static auto pos = Position<id + 1, U, Ts...>::pos;
    };

    // 模板特化：找到了！
    template <int64_t id, typename T, typename... Ts>
    struct Position<id, T, T, Ts...>
    {
        // T 和当前 Head 匹配，返回当前 id
        constexpr static auto pos = id;
    };

    // 最终别名，简化使用
    template <typename FindT, typename... Ts>
    constexpr auto find_idx_by_type = Position<0, FindT, Ts...>::pos;
}

// 使用示例：
static_assert(find_idx_by_type<float, int, double, float> == 2); // 编译期计算```
```

这个 `Position` 结构体通过模板特化和递归，在编译期间“遍历”类型列表，<br>
一旦找到匹配的类型，就通过特化版本“返回”当前的索引 `id`。

#### 2.2 按索引查找类型：`find_type_by_idx`

这个过程反过来同样重要，我们后续会需要它。

```cpp
namespace variant_utils
{
    template <size_t id, typename... Ts>
    struct find_type_by_idx;

    // 特化：id 为 0，就是当前 Head
    template <typename Head, typename... Ts>
    struct find_type_by_idx<0, Head, Ts...>
    {
        using type = Head;
    };

    // 递归：id 不为 0，去剩下的 Ts... 里找 id - 1
    template <size_t id, typename Head, typename... Ts>
    struct find_type_by_idx<id, Head, Ts...>
    {
        using type = typename find_type_by_idx<id - 1, Ts...>::type;
    };
    
    // 最终别名
    template <size_t id, typename... Ts>
    using find_type_by_idx_t = typename find_type_by_idx<id, Ts...>::type;
}

// 使用示例：
static_assert(std::is_same_v<find_type_by_idx_t<1, int, std::string>, std::string>);
```

有了这两个元编程工具，我们的 `Variant` 就拥有了在类型和索引之间自由穿梭的能力。

---

## 三、核心操作：在“收纳盒”中存取物品

现在，我们有了 `Storage` 和索引工具，是时候实现对 `Storage` 内部值的存取了。

### 第三步：访问 `Storage` 的值 - `get_storage_value`

我们需要一个函数，给定一个 `Storage` 和一个索引 `id`，它能返回对应位置的引用。

这里，C++17 的 **`if constexpr`** 是我们的完美武器。

```cpp
namespace variant_utils
{
    template <size_t id, typename Head, typename... Ts>
    auto &get_storage_value(Storage<Head, Ts...> &storage)
    {
        if constexpr (id == 0)
            return storage.value; // id 是 0，就是当前 value
        else
            // id 不是 0，就去 next 里找 id - 1
            return get_storage_value<id - 1, Ts...>(storage.next);
    }
}
```

#### 设计细节剖析：

`if constexpr` 的魔力在于，它是在 **编译期** 进行判断的。<br>
对于一次调用 `get_storage_value<2, ...>`，编译器在编译时就会直接将 `if constexpr (2 == 0)` 判为 `false`，<br>
于是 `return storage.value;` 这行代码 **根本不会被编译**，直接生成递归调用 `get_storage_value<1, ...>` 的代码。

这使得最终生成的代码极其高效，等同于手写 `storage.next.next.value`，没有任何运行时的 `if` 判断开销。

### 第四步：对象的生与死 - 构造与析构

我们不能简单地对 `Storage` 进行赋值。

对于 `std::string` 这样的复杂类型，我们必须精确地调用它的构造函数和析构函数。

#### 4.1 构造 (construct_variant_value)

这里我们需要使用 **`placement new`**。

它允许我们在一个已经分配好的、指定的内存地址上构造一个对象。

```cpp
namespace trait
{
    // 辅助函数，可以移除一个类型的引用和常量属性
    // 在 C++20 中有官方定义，但由于当前实现基于 C++17，我们选择手写一个替代的实现
    template <typename T>
    struct remove_cvref
    {
        using type = std::remove_cv_t<std::remove_reference_t<T>>;
    };

    template <typename T>
    using remove_cvref_t = typename remove_cvref<T>::type;
}

namespace variant_utils
{
    template <typename T, typename Head, typename... Ts>
    void construct_variant_value(Storage<Head, Ts...> &storage, T &&val)
    {
        using type = trait::remove_cvref_t<T>;
        if constexpr (std::is_same_v<type, Head>)
            // 找到了对应的类型槽，使用 placement new 在其上构造对象
            *new (&storage.value) type(std::forward<T>(val));
        else
            // 继续向 next 递归
            construct_variant_value<T, Ts...>(storage.next, std::forward<T>(val));
    }
}
```

#### 4.2 析构 (destroy_variant_value)

与 `placement new` 对应，我们需要 **手动调用析构函数**。

```cpp
namespace variant_utils
{
    template <size_t id, typename Head, typename... Ts>
    void destroy_variant_value(Storage<Head, Ts...> &storage)
    {
        if constexpr (id == 0)
            storage.value.~Head(); // id 是 0，析构当前 value
        else
            destroy_variant_value<id - 1>(storage.next); // 否则，递归析构 next
    }
}
```

通过这两个函数，我们获得了在 `Storage` 这块原始内存上，精确控制任意类型对象生命周期的能力。这是实现一个安全的 `Variant` 的基石。

---

## 四、最终组装：`Variant` 类的诞生

万事俱备，我们现在可以将所有零件组装成最终的 `Variant` 类了。

```cpp
template <typename... Ts>
class Variant
{
private:
    int64_t type_idx{-1}; // 关键：追踪当前类型的索引
    variant_utils::Storage<Ts...> m_storage;

public:
    // 构造函数：接收一个值，并初始化 Variant
    template <
        typename T,
        typename U = trait::remove_cvref_t<T>
        /* ... SFINAE 约束，确保 T 在 Ts... 中 ... */>
    Variant(T &&val)
    {
        // 1. 在编译期找到 T 对应的索引
        constexpr auto idx = variant_utils::find_idx_by_type<U, Ts...>;
        // 2. 在 m_storage 上构造值
        variant_utils::construct_variant_value(m_storage, std::forward<T>(val));
        // 3. 记录索引
        type_idx = idx;
    }

    // get<idx>() 方法
    template <size_t idx>
    auto &get()
    {
        // ... 此处应有运行时检查，确保 idx == type_idx ...
        return variant_utils::get_storage_value<idx>(m_storage);
    }

    // get<T>() 方法
    template <typename T>
    auto &get()
    {
        constexpr auto id = variant_utils::find_idx_by_type<T, Ts...>;
        return get<id>();
    }

    // holds_alternative<T>() 方法
    template <typename T>
    bool holds_alternative() const noexcept
    {
        constexpr auto target_idx = variant_utils::find_idx_by_type<trait::remove_cvref_t<T>, Ts...>;
        return (this->type_idx) == target_idx;
    }

    auto index() const { return type_idx; }

    // ... 析构函数、拷贝/移动构造和赋值将在下一步讲解 ...
};
```

至此，我们的 `Variant` 已经有了基本的构造和访问功能！

它通过 `type_idx` 这一运行时状态，与我们之前构建的一系列编译期元编程工具紧密地结合在了一起。

---

## 五、终极挑战：拷贝、移动与析构的艺术

如果 Variant 持有的是一个 `std::string`，<br>
当 `Variant` 被析构时，我们必须确保 `std::string` 的析构函数被调用。<br>
当 `Variant` 被拷贝时，我们也必须调用 `std::string` 的拷贝构造函数。

一个 `Variant<int, std::string, double>` 对象，它的析构和拷贝行为是动态的，取决于它当前存储的是什么。

我们该如何实现这种动态行为？

### 方案A：用 `switch` 语句 (The Naive Way)

最直观的想法是在析构函数和拷贝构造函数里用一个大的 `switch` 语句：

```cpp
// 伪代码
~Variant()
{
    switch (type_idx)
    {
        case 0: /* 析构 int */ break;
        case 1: get<1>().~string(); break;
        case 2: /* 析构 double */ break;
    }
}
```

这种方法可行，但很丑陋，且每次增删类型都需要修改所有这些 `switch` 语句。

更重要的是，如果类型非常多，`switch` 可能会产生分支预测开销。

同时，它也不能自动地匹配各种不同的模板类型，这是一个模板类型最无法接受的！

### 方案B：函数指针表 (The Pro Move)

有没有更优雅、更高效的方式？答案是肯定的：构建一个 **函数指针表**。

我们可以在编译期创建一个函数指针数组。数组的第 `i` 个元素，指向一个专门用于处理第 `i` 种类型的函数。

#### 5.1 析构 (destroy)

```cpp
private:
    // 定义函数指针类型
    using destroy_func_type = void (*)(void *);

    // 定义一个静态的“销毁函数”模板
    template <typename T>
    static void destroy_value_func_constructor(void *ptr)
    {
        auto &storage = *static_cast<variant_utils::Storage<Ts...> *>(ptr);
        // 通过元编程找到 T 的索引，并调用对应的销毁函数
        variant_utils::destroy_variant_value<variant_utils::find_idx_by_type<T, Ts...>>(storage);
    }
    
    // 关键：在编译期，用类型列表 Ts... 实例化模板，生成一个函数指针数组
    constexpr static destroy_func_type destroy_func[] = {destroy_value_func_constructor<Ts>...};

    void destroy()
    {
        if (type_idx != -1)
            // 运行时，直接用 type_idx 索引到正确的销вищ函数并调用！
            destroy_func[type_idx](&m_storage);
    }

public:
    ~Variant()
    {
        // 如果类型都不是平凡析构的，才需要调用
        if constexpr (!is_all_trivially_destructible)
            destroy();
    }
```

#### 设计细节剖析：

这一招实在是太漂亮了！<br>
我们利用了 C++ 的 **“包展开 (...)”** 语法，`{destroy_value_func_constructor<Ts>...}` 在编译时会被展开成<br>
`{destroy_value_func_constructor<int>, destroy_value_func_constructor<std::string>, ...}`，<br>
从而在编译期就生成了一个完美的、包含了所有析构逻辑的函数指针数组。

在运行时，`destroy()` 函数不再需要任何 `if` 或 `switch`。<br>
它只是一个简单的数组索引操作，这比分支语句要快得多！

#### 5.2 拷贝与移动构造 (construct_from)

同样的思想可以完美地应用在拷贝和移动构造上。

我们可以创建两个表，一个用于拷贝构造，一个用于移动构造。

```cpp
private:
    // ... 定义 construct_from_impl<id> 和 construct_from_impl_move<id> ...

    using constructor_func_type = void (*)(Variant *, const Variant &);
    using move_constructor_func_type = void (*)(Variant *, Variant &&);

    // C++14/17 的 index_sequence 技巧，用于生成 0, 1, 2, ... 序列
    template <std::size_t... I>
    static constexpr std::array<constructor_func_type, sizeof...(Ts)>
    make_constructor_table_impl(std::index_sequence<I...>)
    {
        return {&construct_from_impl<I>...}; // 包展开 I
    }
    static constexpr auto make_constructor_table()
    {
        return make_constructor_table_impl(std::make_index_sequence<sizeof...(Ts)>{});
    }

    // ... 同样的方式创建 make_move_constructor_table ...

    void construct_from(const Variant &other)
    {
        if (other.type_idx == -1) return;
        static constexpr auto table = make_constructor_table();
        table[other.type_idx](this, other); // 同样是 O(1) 的查表调用
        type_idx = other.type_idx;
    }

public:
    Variant(const Variant &other)
    {
        construct_from(other);
    }

    Variant(Variant &&other) noexcept
    {
        // ...
    }
```

通过 `std::make_index_sequence` 这个元编程工具，<br>
我们生成了编译期的整数序列 `0, 1, 2, ...`，并用它来展开模板，<br>
为每一种类型都生成一个特定的拷贝/移动构造函数，并将其地址存入表中。

这套基于函数指针表的设计，是实现高性能多态行为的经典范式，它将运行时的类型判断开销降到了最低。

## 六、总结与展望

恭喜你！我们从一片空白开始，完整地经历了一个功能强大、设计精巧的 `Variant` 类的诞生全过程。

这不仅是一次编码练习，更是一次深入 C++ 底层机制的探索之旅。

我们共同完成了：

1. 数据存储：通过递归联合体 `Storage`，巧妙地解决了异构类型的存储问题。

2. 编译期智能：利用模板元编程实现了类型与索引的自动映射，为类型安全提供了保障。

3. 生命周期管理：深入实践了 `placement new` 和手动析构，并最终通过高效的 函数指针表，为 `Variant` 设计了一套优雅且高性能的拷贝、移动和析构方案。

4. 现代 C++ 特性：我们充分利用了 `if constexpr`, `std::index_sequence`, 包展开等现代 C++ 工具，见证了它们在编写通用库时的巨大威力。

这个项目虽小，但它几乎涵盖了 C++ 模板编程最核心、最精妙的思想。

### 下一步可以做什么？

我们的 Variant 已经非常出色，但追求极致的你，总有新的高峰可以攀登：

- **实现 visit**：<br>
这是 `std::variant` 的核心功能。实现一个 `visit` 函数，<br>
它能接收一个 `Variant` 对象和一个 **可调用对象 (Functor)**，<br>
并根据 `Variant` 的当前类型，以类型安全的方式调用对应的 `operator()`。<br>
这将是对你元编程能力的终极考验。

- **异常安全性**：<br>
研究并实现 **“valueless-by-exception”** 状态。<br>
当 `Variant` 在类型转换的赋值操作中（例如从一个 `string` 赋值为一个可能会抛出异常的对象），<br>
如果构造失败，`Variant` 应该进入一个“无值”的有效状态。

- **与标准库对比**：<br>
阅读你所用编译器（GCC, Clang, MSVC）的 `std::variant` 源码实现，对比它们的设计与你的实现有何异同。<br>
你会发现许多在性能和标准符合性上更为极致的考量。
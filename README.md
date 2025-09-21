# 从零开始：手把手教你实现一个 C++ 的 std::variant (基础数据结构复现实战)

## 前言

你好，C++ 探索者！你是否曾对 `std::variant` 的内部机制感到好奇？<br>
这个 C++17 引入的“类型安全的联合体”，究竟是如何在编译期施展魔法，从而在运行时优雅地存储多种不同类型的值的？

这篇博客就是为你准备的钥匙。我们将一起打开 `variant` 的引擎盖，亲手用 C++ **模板元编程 (Template Metaprogramming)** 的工具，<br>
从最基础的零件开始，一步步打造出我们自己的 `Variant`。

本文不是对标准库文档的复述，而是一份详细的构造蓝图和深度的心路历程。

我们将不断提出在实现过程中遇到的棘手问题，然后用 C++ 最精妙的特性给出解决方案，并深入探讨“为什么”是这样设计的。

### 本文包含的技术实践：

- **模板元编程 (TMP) 入门**：从最基础的编译期阶乘，理解元编程的核心思想：递归与特化。
- **SFINAE (替换失败并非错误)**：深入理解 `std::enable_if`，学会如何为模板编写精确的“启用/禁用”规则。
- **递归模板与参数包展开**：学习如何通过递归和 C++17 折叠表达式，优雅地处理可变参数模板包 (`Ts...`)。
- **`if constexpr` 的威力**：见证 C++17 的编译期 `if` 如何彻底改变模板代码的编写方式。
- **手动生命周期管理**：彻底理解 **placement new** 和 **显式析构函数调用**，真正掌控对象的诞生与消亡。
- **函数指针表优化**：学习一种高级技巧，如何用函数指针数组替代 `switch` 语句，实现 O(1) 复杂度的类型分发。
- **`std::index_sequence`**：揭开这个元编程神器的神秘面纱，看它如何辅助我们生成编译期代码。

### 本文不包含的技术内容：

- `std::variant` 的 `visit` 功能实现。

- 异常安全性的深入探讨 (例如 valueless_by_exception)。

读完本文，你将收获的不仅仅是一个可用的 Variant 类，更重要的是：

- **元编程思维**：学会如何“在编译期思考”，将问题从运行时解决，转移到编译期。
- **设计决策的权衡**：理解在实现一个通用组件时，背后隐藏的性能与复杂度的博弈。
- **从底层构建的成就感**：亲手实现一个标准库级别的组件，彻底掌握它的每一个细节。

现在，让我们收起对模板的敬畏，正式开始这场激动人心的构建之旅。

## 一、项目背景：为什么我们要“造轮子”？

**联合体 (`union`)** 是 C 语言留给我们的一份遗产。

它允许在同一块内存中存储不同类型的数据，以达到节省空间的目的。但它也是一把臭名昭著的双刃剑：

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
它本质上是一个“被增强的”联合体，<br>
它额外携带了一个 **“标签” (discriminator)**，在任何时候都明确地知道自己存储的是什么类型。

既然标准库已经有了，我们为什么还要自己造一个呢？

1. **深度学习**：自己动手实现一遍，是理解模板元编程、类型系统和对象生命周期最深刻的方式。
2. **面试加分项**：在求职面试中，能够清晰阐述 `variant` 的实现原理，绝对是你 C++ 内功深厚的最佳证明。
3. **揭开魔法的面纱**：让你在使用 `std::variant` 时，不再把它看作一个黑盒，而是能清晰地预见其性能和行为。

## 二、第零步：热身 - 模板元编程的核心思想

在深入 `Variant` 之前，我们必须先掌握它的核心技术——**模板元编程（TMP）**。

简单来说，TMP 就是 “在编译期运行的代码”。<br>
它的计算在编译时完成，结果要么是新的类型，要么是编译期常量，**没有任何运行时开销**。

TMP 的最核心模式就是：**泛化用于递归，特化用于终止**。

我们来看一个最简单的例子：编译期计算阶乘。

```cpp
// 泛化版本（递归）：计算 N! 等于 N * (N-1)!
template <int N>
struct Factorial
{
    static constexpr int value = N * Factorial<N - 1>::value;
};

// 特化版本（终止）：0! 的值为 1
template <>
struct Factorial<0>
{
    static constexpr int value = 1;
};

// 使用：
static_assert(Factorial<5>::value == 120); // 这行代码在编译时计算
```

看到了吗？我们用 `struct` 和模板，诱导编译器为我们进行了一场递归计算。

`Variant` 的实现，本质上就是将这种思想应用在“类型”上，进行类型的查找、转换和选择。

## 三、蓝图设计：构建一个类型安全的“魔方”

我们的目标是构建一个类 `Variant<Ts...>`，它能做到：

- **存储**：在内部的某块内存上，存储 `Ts...` 类型列表中的任意一种。
- **追踪**：拥有一个成员变量 (如 `type_idx`)，用于记录当前存储的是第几种类型。
- **构造与访问**：能被 `Ts...` 中的任意类型构造，并提供安全的 `get()` 方法。
- **生命周期**：正确地管理所存储对象的构造、析构、拷贝和移动。

### 第一步：数据的“收纳盒” - 递归的 `Storage`

第一个问题是：如何设计一块能容纳 `int`, `std::string` 等大小不一、行为各异的类型的内存空间？

一个简单的 `union` 对 **非平凡类型**（如 `std::string`）限制太多。<br>
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
        union {
            Head value;            // 要么存储当前类型 Head
            Storage<Ts...> next;   // 要么存储包含剩下类型的 "下一个收纳盒"
        };
    };
}
```

这个设计非常精妙，它像一个“俄罗斯套娃”：

-   `Storage<int, std::string, bool>` 是一个联合体，它可以存放一个 `int`，或者一个 `Storage<std::string, bool>`。
-   而 `Storage<std::string, bool>` 又是一个联合体，它可以存放一个 `std::string`，或者一个 `Storage<bool>`。
-   以此类推，直到空的 `Storage<>`。

通过这种方式，我们巧妙地将所有类型“串”在了一个递归的联合体结构里。<br>
这块内存的总大小，将由 `Ts...` 中最大的那个类型决定。

### 第二步：编译期的“户口本” - 类型与索引的映射

有了“收纳盒” `Storage`，但我们如何知道 `std::string` 应该访问 `storage.next.value` 呢？

我们需要在 **编译期** 就建立好类型和它在参数包中位置（索引）的对应关系。

#### 2.1 按类型查找索引：`find_idx_by_type`

```cpp
namespace variant_utils
{
    // 泛化版本（递归）：没找到，就去剩下的 Ts... 里继续找，id + 1
    template <int64_t id, typename U, typename T, typename... Ts>
    struct Position
    {
        constexpr static auto pos = Position<id + 1, U, Ts...>::pos;
    };

    // 特化版本（终止）：找到了！T 和当前 Head 匹配，返回当前 id
    template <int64_t id, typename T, typename... Ts>
    struct Position<id, T, T, Ts...>
    {
        constexpr static auto pos = id;
    };

    // 最终别名，简化使用
    template <typename FindT, typename... Ts>
    constexpr auto find_idx_by_type = Position<0, FindT, Ts...>::pos;
}

// 使用示例：
static_assert(find_idx_by_type<float, int, double, float> == 2); // 编译期断言
```

这正是我们热身时学的 TMP 模式：<br>
`Position` 结构体通过模板特化和递归，在编译期间“遍历”类型列表，<br>
一旦找到匹配的类型，就通过特化版本“返回”当前的索引 `id`。

#### 2.2 按索引查找类型：`find_type_by_idx`

反过来，按索引找类型同样重要。

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

## 四、核心操作：在“收纳盒”中存取物品

### 第三步：访问 `Storage` 的值 - `get_storage_value`

我们需要一个函数，给定 `Storage` 和索引 `id`，返回对应位置的引用。

这里，C++17 的 `if constexpr` 是我们的完美武器。

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

`if constexpr` 的魔力在于，它是在 **编译期** 进行判断的。<br>
对于 `get_storage_value<2, ...>` 调用，编译器直接将 `if constexpr (2 == 0)` 判为 `false`，<br>
于是 `return storage.value;` 这行代码根本不会被编译，直接生成递归调用 `get_storage_value<1, ...>` 的代码。<br>
这使得最终生成的代码极其高效，等同于手写 `storage.next.next.value`，没有任何运行时 `if` 判断开销。

### 第四步：对象的生与死 - 构造与析构

对于 `std::string` 这样的复杂类型，我们必须精确地调用它的构造函数和析构函数。

#### 4.1 构造 (`construct_variant_value`)

这里我们需要使用 **placement new**，它允许我们在一个已经分配好的、指定的内存地址上构造一个对象。

```cpp
namespace variant_utils
{
    template <typename T, typename Head, typename... Ts>
    void construct_variant_value(Storage<Head, Ts...> &storage, T &&val)
    {
        // 获取 T 的纯粹类型，例如 const string& -> string
        using type = trait::remove_cvref_t<T>;
        if constexpr (std::is_same_v<type, Head>)
            // 找到了对应的类型槽，使用 placement new 在其上构造对象
            new (&storage.value) type(std::forward<T>(val));
        else
            // 继续向 next 递归
            construct_variant_value<T, Ts...>(storage.next, std::forward<T>(val));
    }
}
```

#### 4.2 析构 (`destroy_variant_value`)

与 **placement new** 对应，我们需要 **手动调用析构函数**。

```cpp
namespace variant_utils
{
    template <size_t id, typename Head, typename... Ts>
    void destroy_variant_value(Storage<Head, Ts...> &storage)
    {
        if constexpr (id == 0)
            storage.value.~Head(); // id 是 0，析构当前 value
        else
            destroy_variant_value<id - 1, Ts...>(storage.next); // 否则，递归析构 next
    }
}
```

通过这两个函数，我们获得了在 `Storage` 这块原始内存上，精确控制任意类型对象生命周期的能力。

## 五、最终组装：`Variant` 类的诞生

### 第五步：`Variant` 类的初版骨架

万事俱备，我们来组装 `Variant` 类。

```cpp
template <typename... Ts>
class Variant
{
private:
    int64_t type_idx{-1};
    variant_utils::Storage<Ts...> m_storage;

public:
    // 构造函数将在下一步细化
    template <typename T>
    Variant(T &&val)
    {
        using U = trait::remove_cvref_t<T>;
        constexpr auto idx = variant_utils::find_idx_by_type<U, Ts...>;
        variant_utils::construct_variant_value(m_storage, std::forward<T>(val));
        type_idx = idx;
    }

    template <size_t idx>
    auto &get()
    {
        // 此处应有运行时检查，确保 idx == type_idx
        if (idx != type_idx) throw std::bad_variant_access{};
        return variant_utils::get_storage_value<idx>(m_storage);
    }

    template <typename T>
    auto &get()
    {
        constexpr auto id = variant_utils::find_idx_by_type<T, Ts...>;
        return get<id>();
    }

    // ... 其他方法 ...
};
```

这个骨架已经有了基本功能，但它的构造函数隐藏着一个巨大的陷阱。

### 第六步：**SFINAE** 的应用 - 约束构造函数

考虑以下代码：

`Variant<int, std::string> v1(10); // OK`<br>
`Variant<int, std::string> v2(v1); // 糟糕！`

我们期望第二行调用拷贝构造函数，但它却会匹配到 `template <typename T> Variant(T &&val)` 这个“贪婪”的模板构造函数。
> 此时 `T` 被推导为 `Variant<int, std::string>&`

我们需要一种方法，告诉编译器：<br>
当 `T` 是 `Variant` 本身时，请“禁用”这个模板构造函数。

这就是 **SFINAE (Substitution Failure Is Not An Error)** 的用武之地。

`SFINAE` 的核心思想是：<br>
当编译器尝试用一个类型去替换模板参数时，如果替换失败了（比如访问了一个不存在的成员），这并不会导致编译错误，而仅仅是将这个模板函数从重载候选集中移除。`std::enable_if` 就是利用这个原理的工具。

修改后的构造函数如下：

```cpp
public:
    template <
        typename T,
        // 使用 remove_cvref_t 获取 T 的纯净类型
        typename U = trait::remove_cvref_t<T>,
        // SFINAE 规则 1: 确保 U 不是 Variant 本身
        std::enable_if_t<!std::is_same_v<U, Variant>, int> = 0,
        // SFINAE 规则 2: 确保 U 在类型列表 Ts... 中
        std::enable_if_t<(variant_utils::find_idx_by_type<U, Ts...> != -1), int> = 0
    >
    Variant(T &&val)
    {
        constexpr auto idx = variant_utils::find_idx_by_type<U, Ts...>;
        variant_utils::construct_variant_value(m_storage, std::forward<T>(val));
        type_idx = idx;
    }
```

现在，当 `v2(v1)` 尝试匹配这个模板时，<br>
`U` 是 `Variant`，第一个 `enable_if` 规则失败，<br>
这个构造函数被编译器“无视”，从而正确地选择了我们将在下一步实现的拷贝构造函数。

## 六、终极挑战：拷贝、移动与析构的艺术

如果 `Variant` 持有一个 `std::string`，<br>
当 `Variant` 被析构、拷贝时，我们必须确保 `std::string` 的对应方法被调用。<br>
这种行为是动态的，取决于 `type_idx`。

### 方案 A：用 `switch` 语句 (The Naive Way)

最直观的想法是在析构和拷贝函数里用 `switch` 语句：

```cpp
// 伪代码
~Variant()
{
    switch (type_idx)
    {
        case 0: /* 析构第0个类型 */ break;
        case 1: get<1>().~string(); break;
        // ...
    }
}
```

这种方法可行，但很丑陋，每次增删类型都要修改所有 `switch`，且无法处理任意模板类型，对模板库来说是不可接受的。

### 方案 B：函数指针表 (The Pro Move)

更优雅、高效的方式是：在编译期创建一个函数指针表。<br>
数组的第 `i` 个元素，指向一个专门处理第 `i` 种类型的函数。

#### 6.1 析构 (destroy)
```cpp
private:
    // 定义函数指针类型
    using destroy_func_type = void (*)(void *);

    // 定义一个静态的“销毁函数”模板
    template <typename T>
    static void destroy_value_func_constructor(void *ptr)
    {
        // ... 内部通过元编程找到 T 的索引，并调用对应的销毁函数
        constexpr auto idx = variant_utils::find_idx_by_type<T, Ts...>;
        variant_utils::destroy_variant_value<idx>(*static_cast<variant_utils::Storage<Ts...>*>(ptr));
    }
    
    // 关键：在编译期，用类型列表 Ts... 实例化模板，生成一个函数指针数组
    constexpr static destroy_func_type destroy_func_table[] = { &destroy_value_func_constructor<Ts>... };

    void destroy()
    {
        if (type_idx != -1)
            // 运行时，直接用 type_idx 索引到正确的函数并调用！
            destroy_func_table[type_idx](&m_storage);
    }
public:
    ~Variant() {
        destroy();
    }
```

这一招太漂亮了！我们利用了 C++ 的 **“包展开 (...)”** 语法，`{ &destroy_value_func_constructor<Ts>... }` 在编译时会被展开成 `{ &destroy<int>, &destroy<string>, ...}`，从而在编译期生成一个完美的函数指针数组。

运行时，`destroy()` 函数不再需要任何 `if` 或 `switch`，只是一个 O(1) 复杂度的数组索引操作，这比有分支预测开销的 `switch` 快得多！

#### 6.2 拷贝与移动构造 (construct_from)

同样的思想可以完美地应用在拷贝和移动构造上。

但我们如何生成 `{ &copy_impl<0>, &copy_impl<1>, ... }` 这样的函数指针呢？

这里需要另一个元编程神器：`std::index_sequence`。它是一个能产生编译期整数序列 `0, 1, 2, ...` 的工具。

```cpp
private:
    // ... 定义 construct_from_impl<id> (负责拷贝) 和 construct_from_impl_move<id> (负责移动) ...

    using constructor_func_type = void (*)(Variant *, const Variant &);

    // C++14/17 的 index_sequence 技巧，用于生成 0, 1, 2, ... 序列
    template <std::size_t... I>
    static constexpr std::array<constructor_func_type, sizeof...(Ts)>
    make_constructor_table_impl(std::index_sequence<I...>)
    {
        return { &construct_from_impl<I>... }; // 包展开 I -> 0, 1, 2...
    }
    static constexpr auto make_constructor_table()
    {
        return make_constructor_table_impl(std::make_index_sequence<sizeof...(Ts)>{});
    }
    
    // ... 同样的方式创建移动构造的表 ...

public:
    Variant(const Variant &other)
    {
        if (other.type_idx == -1) return;
        // 编译期生成函数表
        static constexpr auto table = make_constructor_table();
        // 运行时 O(1) 查表调用
        table[other.type_idx](this, other);
        type_idx = other.type_idx;
    }
    // ... 移动构造函数类似 ...
```

通过 `std::make_index_sequence`，我们生成了编译期的整数序列，并用它来展开模板，为每一种类型都生成一个特定的拷贝/移动函数，并将其地址存入表中。

这套设计是实现高性能多态行为的经典范式。

## 七、扩展篇：将优雅进行到底 - 实现 `operator==`

函数指针表的威力不止于此。我们甚至可以用它来实现 `operator==`。

**挑战**：`variant == variant` 的比较逻辑也取决于两者动态存储的类型。

**思路**：再次创建一个函数指针表 `compare_func_table`。

```cpp
// 1. 先创建一个元编程工具，判断类型是否支持 ==
namespace trait
{
    template <typename T, typename = void>
    struct is_equality_comparable : std::false_type {};

    template <typename T>
    struct is_equality_comparable<T, std::void_t<decltype(std::declval<T>() == std::declval<T>())>> : std::true_type {};
}

// 2. 创建比较函数模板
template <size_t I>
static bool compare_value_func(const void *self, const void *other)
{
    using type = variant_utils::find_type_by_idx_t<I, Ts...>;
    // 再次利用 if constexpr 处理不支持比较的类型
    if constexpr (trait::is_equality_comparable<type>::value)
    {
        // ... 获取值并比较 ...
    }
    else
        return false; // 或者 static_assert 报错
}

// 3. 创建函数指针表
// ... 类似 make_constructor_table 的方式创建 compare_table ...

// 4. 实现 operator==
bool operator==(const Variant &other) const
{
    // C++17 折叠表达式，编译期检查所有类型是否都可比较
    static_assert((trait::is_equality_comparable<Ts>::value && ...), "All types must be comparable");

    if (type_idx != other.type_idx) return false;
    if (type_idx == -1) return true;
    
    // 运行时 O(1) 查表调用
    return compare_table[type_idx](&m_storage, &other.m_storage);
}
```

这个例子完美地展示了该设计模式的强大复用性，并利用了 C++17 的折叠表达式 `(... && ...)` 在编译期对所有类型进行了一次优雅的静态检查。

## 八、总结与展望

恭喜你！我们从一片空白开始，完整地经历了一个功能强大、设计精巧的 `Variant` 类的诞生全过程。

我们共同完成了：

1. **数据存储**：通过递归联合体 `Storage`，巧妙地解决了异构类型的存储问题。
2. **编译期智能**：利用模板元编程实现了类型与索引的自动映射，并通过 **SFINAE** 解决了构造函数重载的歧义。
3. **生命周期管理**：深入实践了 **placement new** 和手动析构，并最终通过高效的 函数指针表，为 `Variant` 设计了一套优雅且高性能的拷贝、移动、析构和比较方案。
4. 现代 C++ 特性：我们充分利用了 `if constexpr`, `std::index_sequence`, **包展开**, **折叠表达式** 等现代 C++ 工具，见证了它们在编写通用库时的巨大威力。

### 下一步可以做什么？

- **实现 `visit`**：这是 `std::variant` 的核心功能。<br>
实现一个 `visit` 函数，它能接收一个 `Variant` 对象和一个 **可调用对象 (Functor)**，并根据 `Variant` 的当前类型，以类型安全的方式调用对应的 `operator()`。<br>
这将是对你元编程能力的终极考验。
- 异常安全性：研究并实现 **“valueless-by-exception”** 状态。<br>
当 `Variant` 在类型转换的赋值操作中，如果构造失败，`Variant` 应该进入一个“无值”的有效状态。
- 与标准库对比：阅读你所用编译器（GCC, Clang, MSVC）的 `std::variant` 源码实现，对比它们的设计与你的实现有何异同。<br>你会发现许多在性能和标准符合性上更为极致的考量。

源码已经放在了 [github 仓库](https://github.com/pjh456/Let_us_build_cpp_variant) 中，如果这篇博客对你有帮助，可以给仓库点个 star 支持一下！
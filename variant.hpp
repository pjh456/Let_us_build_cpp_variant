#ifndef INCLUDE_VARIANT
#define INCLUDE_VARIANT

#include <cstdint>
#include <array>
#include <utility>
#include <type_traits>

namespace trait
{
    template <typename T>
    struct remove_cvref
    {
        using type = std::remove_cv_t<std::remove_reference_t<T>>;
    };

    template <typename T>
    using remove_cvref_t = typename remove_cvref<T>::type;

    template <typename>
    inline constexpr bool always_false_v = false;

    template <typename>
    inline constexpr bool always_true_v = true;
}

namespace variant_utils
{
    template <typename... Ts>
    struct is_all_trivially_destructible;
    template <>
    struct is_all_trivially_destructible<> : std::true_type
    {
    };
    template <typename T, typename... Ts>
    struct is_all_trivially_destructible<T, Ts...>
        : std::integral_constant<bool, std::is_trivially_destructible_v<T> && is_all_trivially_destructible<Ts...>::value>
    {
    };
    template <typename... Ts>
    constexpr auto is_all_trivially_destructible_v = is_all_trivially_destructible<Ts...>::value;

    template <int64_t id, typename... Ts>
    struct Position;
    template <int64_t id, typename U, typename T, typename... Ts>
    struct Position<id, U, T, Ts...>
    {
        // constexpr static auto pos = (constexpr id > sizeof...(Ts)) ? -1 : (constexpr std::is_same_v<U, T> ? id : Position<id + 1, U, Ts...>::pos);
        constexpr static auto pos = Position<id + 1, U, Ts...>::pos;
    };
    template <int64_t id, typename T, typename... Ts>
    struct Position<id, T, T, Ts...>
    {
        constexpr static auto pos = id;
    };
    template <int64_t id, typename T>
    struct Position<id, T>
    {
        constexpr static auto pos = -1;
    };
    template <typename FindT, typename... Ts>
    constexpr auto find_idx_by_type = Position<0, FindT, Ts...>::pos;

    template <size_t id, typename... Ts>
    struct find_type_by_idx;
    template <typename Head, typename... Ts>
    struct find_type_by_idx<0, Head, Ts...>
    {
        using type = Head;
    };
    template <size_t id, typename Head, typename... Ts>
    struct find_type_by_idx<id, Head, Ts...>
    {
        static_assert(id <= sizeof...(Ts), "id is out of range!");
        using type = typename find_type_by_idx<id - 1, Ts...>::type;
    };
    template <size_t id, typename... Ts>
    using find_type_by_idx_t = typename find_type_by_idx<id, Ts...>::type;

    template <typename... Ts>
    struct Storage;
    template <>
    struct Storage<>
    {
    };
    template <typename Head, typename... Ts>
    struct Storage<Head, Ts...>
    {
        union
        {
            Head value;
            Storage<Ts...> next;
        };
        Storage() {}
        ~Storage() noexcept {}
        Storage(const Storage &) = default;
        Storage(Storage &&) noexcept = default;
        Storage &operator=(const Storage &) = default;
        Storage &operator=(Storage &&) noexcept = default;
    };

    template <size_t id, typename... Ts>
    auto &get_storage_value(Storage<Ts...> &);
    template <size_t id, typename... Ts, std::enable_if_t<id >= sizeof...(Ts), int> = 0>
    auto &get_storage_value(Storage<Ts...> &) { static_assert(trait::always_false_v<Ts...>, "id is out of range!"); }
    template <size_t id, typename Head, typename... Ts>
    auto &get_storage_value(Storage<Head, Ts...> &storage)
    {
        if constexpr (id == 0)
            return storage.value;
        else
            return get_storage_value<id - 1, Ts...>(storage.next);
    }

    template <size_t id, typename... Ts>
    const auto &get_storage_value(const Storage<Ts...> &);
    template <size_t id, typename... Ts, std::enable_if_t<id >= sizeof...(Ts), int> = 0>
    const auto &get_storage_value(const Storage<Ts...> &) { static_assert(trait::always_false_v<Ts...>, "id is out of range!"); }
    template <size_t id, typename Head, typename... Ts>
    const auto &get_storage_value(const Storage<Head, Ts...> &storage)
    {
        if constexpr (id == 0)
            return storage.value;
        else
            return get_storage_value<id - 1, Ts...>(storage.next);
    }

    template <typename T, typename... Ts>
    void construct_variant_value(Storage<Ts...> &, T &&);
    template <typename T, typename... Ts, std::enable_if_t<(find_idx_by_type<T, Ts...> == -1), int> = 0>
    void construct_variant_value(Storage<T> &, T &&) { static_assert(trait::always_false_v<Ts...>, "Can't find type T in Ts...!"); }
    template <typename T>
    void construct_variant_value(Storage<T> &storage, T &&val)
    {
        using type = trait::remove_cvref_t<T>;
        *new (&storage.value) type(std::forward<T>(val));
    }
    template <typename T, typename Head, typename... Ts>
    void construct_variant_value(Storage<Head, Ts...> &storage, T &&val)
    {
        using type = trait::remove_cvref_t<T>;
        if constexpr (std::is_same_v<type, Head>)
            *new (&storage.value) type(std::forward<T>(val));
        else
            construct_variant_value<T, Ts...>(storage.next, std::forward<T>(val));
    }

    template <size_t id, typename... Ts>
    void destroy_variant_value(Storage<Ts...> &);
    template <size_t id, typename... Ts, std::enable_if_t<(id >= sizeof...(Ts)), int> = 0>
    void destroy_variant_value(Storage<Ts...> &) { static_assert(trait::always_false_v<Ts...>, "id is out of range!"); }
    template <size_t id, typename Head, typename... Ts>
    void destroy_variant_value(Storage<Head, Ts...> &storage)
    {
        if constexpr (id == 0)
            storage.value.~Head();
        else
            destroy_variant_value<id - 1>(storage.next);
    }
}

template <typename... Ts>
class Variant
{
public:
    constexpr static auto is_all_trivially_destructible = variant_utils::is_all_trivially_destructible_v<Ts...>;
    constexpr static auto m_size = sizeof...(Ts);
    constexpr static auto null_type = -1;

    // template <typename...>
    // friend class Variant;

private:
    int64_t type_idx{null_type};
    variant_utils::Storage<Ts...> m_storage;

public:
    template <size_t idx>
    auto &get() { return variant_utils::get_storage_value<idx>(m_storage); }

    template <size_t idx>
    const auto &get() const { return variant_utils::get_storage_value<idx>(m_storage); }

    template <typename T>
    auto &get()
    {
        constexpr auto id = variant_utils::find_idx_by_type<T, Ts...>;
        static_assert(id != -1);
        return get<id>();
    }

    template <typename T>
    const auto &get() const
    {
        constexpr auto id = variant_utils::find_idx_by_type<T, Ts...>;
        static_assert(id != -1);
        return get<id>();
    }

public:
    template <size_t id>
    bool hold() const { return id == type_idx; }

    template <typename T, typename U = trait::remove_cvref_t<T>>
    bool hold() const
    {
        return std::is_same_v<U, variant_utils::find_type_by_idx_t<type_idx, Ts...>>;
    }

    template <size_t id>
    bool holds_alternative() const { return id == type_idx; }

    template <typename T>
    bool holds_alternative() const noexcept
    {
        constexpr auto target_idx = variant_utils::find_idx_by_type<trait::remove_cvref_t<T>, Ts...>;

        if constexpr (target_idx == -1)
            return false;

        return (this->type_idx) == target_idx;
    }

    auto index() const { return type_idx; }

private:
    using destroy_func_type = void (*)(void *);
    template <typename T>
    static void destroy_value_func_constructor(void *ptr)
    {
        auto &storage = *static_cast<variant_utils::Storage<Ts...> *>(ptr);
        variant_utils::destroy_variant_value<variant_utils::find_idx_by_type<T, Ts...>>(storage);
    }
    constexpr static destroy_func_type destroy_func[] = {destroy_value_func_constructor<Ts>...};
    void destroy()
    {
        if (type_idx != null_type)
            destroy_func[type_idx](&m_storage);
    }

private:
    template <size_t id>
    static void construct_from_impl(Variant *self, const Variant &other)
    {
        using type = variant_utils::find_type_by_idx_t<id, Ts...>;
        auto &place = other.template get<id>();
        new (&self->template get<id>()) type(place);
    }

    template <size_t id>
    static void construct_from_impl_move(Variant *self, Variant &&other)
    {
        using type = variant_utils::find_type_by_idx_t<id, Ts...>;
        auto &place = self->template get<id>();
        new (&place) type(std::move(other.template get<id>()));
    }

    using constructor_func_type = void (*)(Variant *, const Variant &);
    using move_constructor_func_type = void (*)(Variant *, Variant &&);

    template <std::size_t... I>
    static constexpr std::array<constructor_func_type, sizeof...(Ts)>
    make_constructor_table_impl(std::index_sequence<I...>) { return {&construct_from_impl<I>...}; }
    static constexpr auto make_constructor_table() { return make_constructor_table_impl(std::make_index_sequence<sizeof...(Ts)>{}); }

    template <std::size_t... I>
    static constexpr std::array<move_constructor_func_type, sizeof...(Ts)>
    make_move_constructor_table_impl(std::index_sequence<I...>) { return {&construct_from_impl_move<I>...}; }
    static constexpr auto make_move_constructor_table() { return make_move_constructor_table_impl(std::make_index_sequence<sizeof...(Ts)>{}); }

    void construct_from(const Variant &other)
    {
        type_idx = null_type;

        if (other.type_idx == null_type)
            return;

        static constexpr auto table = make_constructor_table();
        table[other.type_idx](this, other);

        type_idx = other.type_idx;
    }

    void construct_from(Variant &&other)
    {
        type_idx = null_type;

        if (other.type_idx == null_type)
            return;

        static constexpr auto table = make_move_constructor_table();
        table[other.type_idx](this, std::move(other));

        type_idx = other.type_idx;
    }

public:
    Variant() {};

    template <
        typename T,
        typename U = trait::remove_cvref_t<T>,
        std::enable_if_t<!std::is_same_v<U, Variant>, int> = 0,
        std::enable_if_t<(variant_utils::find_idx_by_type<U, Ts...> != -1), int> = 0>
    Variant(T &&val)
    {
        constexpr auto idx = variant_utils::find_idx_by_type<U, Ts...>;
        variant_utils::construct_variant_value(m_storage, std::forward<T>(val));
        type_idx = idx;
    }

public:
    Variant(const Variant &other)
    {
        construct_from(other);
    }

    Variant(Variant &&other) noexcept
    {
        construct_from(std::move(other));
        other.type_idx = null_type;
    }

    ~Variant()
    {
        if constexpr (!is_all_trivially_destructible)
            destroy();
    }

public:
    template <
        typename T,
        typename U = trait::remove_cvref_t<T>,
        std::enable_if_t<!std::is_same_v<U, Variant>, int> = 0,
        std::enable_if_t<(variant_utils::find_idx_by_type<U, Ts...> != -1), int> = 0>
    Variant &operator=(T &&val)
    {
        destroy();
        type_idx = null_type;

        constexpr auto idx = variant_utils::find_idx_by_type<U, Ts...>;
        variant_utils::construct_variant_value(m_storage, std::forward<T>(val));
        type_idx = idx;
        return *this;
    }

    Variant &operator=(const Variant &other)
    {
        if (this == &other)
            return *this;

        destroy();

        construct_from(other);

        return *this;
    }

    Variant &operator=(Variant &&other) noexcept
    {
        if (this == &other)
            return *this;

        destroy();

        construct_from(std::move(other));

        other.type_idx = null_type;

        return *this;
    }
};

#endif // INCLUDE_VARIANT
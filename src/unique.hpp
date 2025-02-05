#pragma once

// #################################################################################################

// A unique type generator
// Marking *all* Unique<> types as [[nodiscard]] is maybe overkill, but there is no way of marking a specific instanciation
template <typename T, typename TAG, typename specialization = void>
class [[nodiscard]] Unique
{
    private:
    T val; // no default initialization so that std::is_trivial_v<Unique<T>> == true

    public:
    using self = Unique<T, TAG, specialization>;
    using type = T;

    ~Unique() = default;
    constexpr explicit Unique(T b_)
    : val{ b_ }
    {
    }

    // rule of 5
    constexpr Unique() = default;
    constexpr Unique(Unique const&) = default;
    constexpr Unique(Unique&&) = default;
    constexpr Unique& operator=(Unique const&) = default;
    constexpr Unique& operator=(Unique&&) = default;

    // Forbid construction with any other class, especially with types that convert naturally to T.
    // For instance, this prevents construction with a float when T is an integer type.
    // Conversion will have to be explicit and the developer will be aware of it.
    // However we want to keep the same-type copy constructors (including with an inherited class), hence the enable_if stuff.
    template <typename AnyOtherClass, std::enable_if_t<!std::is_base_of_v<self, std::decay_t<AnyOtherClass>>, bool> = true>
    Unique(AnyOtherClass const&) = delete;
    template <typename AnyOtherClass, std::enable_if_t<!std::is_base_of_v<self, std::decay_t<AnyOtherClass>>, bool> = true>
    Unique(AnyOtherClass&&) = delete;

    // can't implicitly affect from base type
    Unique& operator=(T const&) = delete;
    constexpr Unique& operator=(T&&) = delete;

    // cast
    constexpr operator T const&() const noexcept { return val; }
    constexpr T const& value() const noexcept { return val; }

    // pre-increment
    auto& operator++() noexcept
    {
        ++val;
        return *this;
    }
    // post-increment
    auto operator++(int) noexcept
    {
        return Unique<T, TAG>{ std::exchange(val, val + 1) };
    }

    // pre-decrement
    auto& operator--() noexcept
    {
        --val;
        return *this;
    }
    // post-decrement
    auto operator--(int) noexcept
    {
        return Unique<T, TAG>{ std::exchange(val, val - 1) };
    }
};

template <typename T, typename TAG>
// Marking *all* Unique<> types as [[nodiscard]] is maybe overkill, but there is no way of marking a specific instanciation
class [[nodiscard]] Unique<T, TAG, std::enable_if_t<!std::is_scalar_v<T>>>
: public T
{
    public:
    using self = Unique<T, TAG, void>;
    using type = T;
    using T::T;
    constexpr explicit Unique(T const& b_)
    : T{ b_ }
    {
    }

    // rule of 5
    constexpr Unique() = default;
    constexpr Unique(Unique const&) = default;
    constexpr Unique(Unique&&) = default;
    constexpr Unique& operator=(Unique const&) = default;
    constexpr Unique& operator=(Unique&&) = default;

    // Forbid construction with any other class, especially with types that convert naturally to T.
    // For instance, this prevents construction with a float when T is an integer type.
    // Conversion will have to be explicit and the developer will be aware of it.
    // However we want to keep the same-type copy constructors (including with an inherited class), hence the enable_if stuff.
    template <typename AnyOtherClass, std::enable_if_t<!std::is_base_of_v<self, std::decay_t<AnyOtherClass>>, bool> = true>
    Unique(AnyOtherClass const&) = delete;
    template <typename AnyOtherClass, std::enable_if_t<!std::is_base_of_v<self, std::decay_t<AnyOtherClass>>, bool> = true>
    Unique(AnyOtherClass&&) = delete;

    // can't implicitly affect from base type
    Unique& operator=(T const&) = delete;
    constexpr Unique& operator=(T&&) = delete;
};

#define DECLARE_UNIQUE_TYPE(_name, _type) using _name = Unique<_type, class Unique_##_name##_Tag>

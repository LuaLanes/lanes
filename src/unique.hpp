#pragma once

// #################################################################################################

// A unique type generator
template <typename T, typename TAG, typename specialization = void>
class Unique
{
    private:
    T val; // no default initialization so that std::is_trivial_v<Unique<T>> == true

    public:
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

    // can't implicitly affect from base type
    Unique& operator=(T const&) = delete;
    constexpr Unique& operator=(T&&) = delete;

    // cast
    constexpr operator T() const noexcept { return val; }

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
};

template <typename T, typename TAG>
class Unique<T, TAG, std::enable_if_t<!std::is_scalar_v<T>>>
: public T
{
    public:
    using type = T;
    using T::T;
    explicit Unique(T const& b_)
    : T{ b_ }
    {
    }
};

#define DECLARE_UNIQUE_TYPE(_name, _type) using _name = Unique<_type, class Unique_##_name##_Tag>

//
//  bpacs.hpp
//  bpacs
//
//  Created by Nikita Ivanov on 02.09.2020.
//  Copyright © 2020 osdever. All rights reserved.
//

#pragma once

#include <type_traits>
#include <cstddef>

namespace bpacs
{
	template<class FieldMeta, class T>
	class field
	{
	public:
		using element_type = T;
		field(T& ref) : _ptr(&ref) {}

		auto index() const { return FieldMeta::index; }
		auto holder() const { return FieldMeta::holder; }
		auto name() const { return FieldMeta::name; }

		T& value() { return *_ptr; }
		const T& value() const { return *_ptr; }

	private:
		T* _ptr;
	};

	template<class FieldMeta, class T>
	class field<FieldMeta, const T>
	{
	public:
		using element_type = const T;
		field(const T& ref) : _ptr(&ref) {}

		auto index() const { return FieldMeta::index; }
		auto holder() const { return FieldMeta::holder; }
		auto name() const { return FieldMeta::name; }

		const T& value() const { return *_ptr; }

	private:
		const T* _ptr;
	};

	template<class Holder, size_t I>
	struct field_meta;

	template<class Holder, size_t I>
	field<field_meta<Holder, I>, typename field_meta<Holder, I>::type> get_field(Holder&);

	template<class Holder, size_t I>
	field<field_meta<Holder, I>, const typename field_meta<Holder, I>::type> get_const_field(const Holder&);


	template<class T, typename = std::void_t<>>
	struct has_bp_reflection : std::false_type {};

	template<class T>
	struct has_bp_reflection<T, std::void_t<typename field_meta<T, 0>::type>> : std::true_type {};

	template<class T, size_t N, typename = std::void_t<>>
	struct field_iterator
	{
		template<class F>
		static void iterate(F&&) {}
	};

	template<class T, size_t N>
	struct field_iterator<T, N, std::void_t<typename field_meta<T, N>::type>>
	{
		template<class F>
		static void iterate(F&& f)
		{
			f(field_meta<T, N>{});
			field_iterator<T, N + 1>::iterate(f);
		}
	};

	template<class T, class F>
	void iterate_fields(F&& f) { field_iterator<T, 0>::iterate(f); }

	template<class T, class F>
	void iterate_object(T& object, F&& f)
	{
		iterate_fields<T>(
			[&object, &f](auto info)
			{
				f(get_field<T, info.index>(object));
			}
		);
	}

	template<class T, class F>
	void iterate_object(const T& object, F&& f)
	{
		iterate_fields<T>(
			[&object, &f](auto info)
			{
				f(get_const_field<T, info.index>(object));
			}
		);
	}
    
    template<class T>
    auto get_class_name()
    {
        return field_meta<T, 0>::holder;
    }
}


#define BP_DEFINE_REFL_FIELD(Holder, Index, Name) \
template<> \
struct bpacs::field_meta<Holder, Index> \
{ \
    static constexpr auto index = Index; \
    static constexpr auto holder = # Holder; \
    static constexpr auto name = # Name; \
    using type = decltype(Holder::Name); \
}; \
\
template<> \
inline ::bpacs::field<::bpacs::field_meta<Holder, Index>, decltype(Holder::Name)> bpacs::get_field<Holder, Index>(Holder& holder) \
{ \
    return ::bpacs::field<::bpacs::field_meta<Holder, Index>, decltype(Holder::Name)>(holder.Name); \
} \
\
template<> \
inline ::bpacs::field<::bpacs::field_meta<Holder, Index>, const decltype(Holder::Name)> bpacs::get_const_field<Holder, Index>(const Holder& holder) \
{ \
    return ::bpacs::field<::bpacs::field_meta<Holder, Index>, const decltype(Holder::Name)>(holder.Name); \
}

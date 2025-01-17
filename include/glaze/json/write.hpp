// Glaze Library
// For the license information refer to glaze.hpp

#pragma once

#include "fmt/format.h"
#include "fmt/compile.h"

#include <charconv>
#include <iterator>
#include <ostream>
#include <variant>

#include "glaze/core/format.hpp"
#include "glaze/util/for_each.hpp"
#include "glaze/util/dump.hpp"
#include "glaze/json/from_ptr.hpp"

#include "glaze/util/to_chars.hpp"
#include "glaze/util/itoa.hpp"
#include "glaze/core/write_chars.hpp"

namespace glz
{
   namespace detail
   {
      template <class T = void>
      struct to_json {};
      
      template <>
      struct write<json>
      {
         template <auto Opts, class T,  is_context Ctx, class B>
         static void op(T&& value, Ctx&& ctx, B&& b) {
            to_json<std::decay_t<T>>::template op<Opts>(std::forward<T>(value), std::forward<Ctx>(ctx), std::forward<B>(b));
         }
         
         template <auto Opts, class T, is_context Ctx, class B, class IX>
         static void op(T&& value, Ctx&& ctx, B&& b, IX&& ix) {
            to_json<std::decay_t<T>>::template op<Opts>(std::forward<T>(value), std::forward<Ctx>(ctx), std::forward<B>(b), std::forward<IX>(ix));
         }
      };
      
      template <boolean_like T>
      struct to_json<T>
      {
         template <auto Opts, class... Args>
         static void op(const bool value, is_context auto&&, Args&&... args) noexcept
         {
            if (value) {
               dump<"true">(std::forward<Args>(args)...);
            }
            else {
               dump<"false">(std::forward<Args>(args)...);
            }
         }
      };
      
      template <num_t T>
      struct to_json<T>
      {
         template <auto Opts, class B>
         static void op(auto&& value, is_context auto&& ctx, B&& b) noexcept
         {
            write_chars::op<Opts>(value, ctx, b);
         }
         
         template <auto Opts, class B>
         static void op(auto&& value, is_context auto&& ctx, B&& b, auto&& ix) noexcept
         {
            write_chars::op<Opts>(value, ctx, b, ix);
         }
      };

      template <class T>
      requires str_t<T> || char_t<T>
      struct to_json<T>
      {
         template <auto Opts>
         static void op(auto&& value, is_context auto&& ctx, auto&& b) noexcept
         {
            dump<'"'>(b);
            const auto write_char = [&](auto&& c) {
               switch (c) {
               case '\\':
               case '"':
                  dump<'\\'>(b);
                  break;
               }
               dump(c, b);
            };
            if constexpr (char_t<T>) {
               write_char(value);
            }
            else {
               const sv str = value;
               for (auto&& c : str) {
                  write_char(c);
               }
            }
            dump<'"'>(b);
         }
         
         template <auto Opts>
         static void op(auto&& value, is_context auto&& ctx, auto&& b, auto&& ix) noexcept
         {
            if constexpr (char_t<T>) {
               dump<'"'>(b, ix);
               switch (value) {
               case '\\':
               case '"':
                  dump<'\\'>(b, ix);
                  break;
               }
               dump(value, b, ix);
               dump<'"'>(b, ix);
            }
            else {
               const sv str = value;
               const auto n = str.size();
               
               // we use 4 * n to handle potential escape characters and quoted bounds (excessive safety)
               if ((ix + 4 * n) >= b.size()) [[unlikely]] {
                  b.resize(std::max(b.size() * 2, ix + 4 * n));
               }
               
               dump_unchecked<'"'>(b, ix);
               
               // now we don't have to check writing
               for (auto&& c : str) {
                  switch (c) {
                  case '\\':
                  case '"':
                     b[ix] = '\\';
                     ++ix;
                     break;
                  }
                  b[ix] = c;
                  ++ix;
               }
               dump_unchecked<'"'>(b, ix);
            }
         }
      };

      template <glaze_enum_t T>
      struct to_json<T>
      {
         template <auto Opts, class... Args>
         static void op(auto&& value, is_context auto&& ctx, Args&&... args) noexcept
         {
            using key_t = std::underlying_type_t<T>;
            static constexpr auto frozen_map =
               detail::make_enum_to_string_map<T>();
            const auto& member_it = frozen_map.find(static_cast<key_t>(value));
            if (member_it != frozen_map.end()) {
               const sv str = {member_it->second.data(),
                                       member_it->second.size()};
               // Note: Assumes people dont use strings with chars that need to
               // be
               // escaped for their enum names
               // TODO: Could create a pre qouted map for better perf
               dump<'"'>(std::forward<Args>(args)...);
               dump(str, std::forward<Args>(args)...);
               dump<'"'>(std::forward<Args>(args)...);
            }
            else [[unlikely]] {
               // What do we want to happen if the value doesnt have a mapped
               // string
               write<json>::op<Opts>(
                  static_cast<std::underlying_type_t<T>>(value), ctx, std::forward<Args>(args)...);
            }
         }
      };

      template <func_t T>
      struct to_json<T>
      {
         template <auto Opts, class... Args>
         static void op(auto&& /*value*/, Args&&...) noexcept
         {}
      };

      template <class T>
      requires std::same_as<std::decay_t<T>, raw_json>
      struct to_json<T>
      {
         template <auto Opts>
         static void op(auto&& value, is_context auto&&, auto&& b) noexcept {
            dump(value.str, b);
         }
         
         template <auto Opts>
         static void op(auto&& value, is_context auto&&, auto&& b, auto&& ix) noexcept {
            dump(value.str, b, ix);
         }
      };
      
      template <array_t T>
      struct to_json<T>
      {
         template <auto Opts, class... Args>
         static void op(auto&& value, is_context auto&& ctx, Args&&... args) noexcept
         {
            dump<'['>(std::forward<Args>(args)...);
            const auto is_empty = [&]() -> bool {
               if constexpr (nano::ranges::sized_range<T>) {
                  return value.size() ? false : true;
               }
               else {
                  return value.empty();
               }
            }();
            
            if (!is_empty) {
               auto it = value.begin();
               write<json>::op<Opts>(*it, ctx, std::forward<Args>(args)...);
               ++it;
               const auto end = value.end();
               for (; it != end; ++it) {
                  dump<','>(std::forward<Args>(args)...);
                  write<json>::op<Opts>(*it, ctx, std::forward<Args>(args)...);
               }
            }
            dump<']'>(std::forward<Args>(args)...);
         }
      };

      template <map_t T>
      struct to_json<T>
      {
         template <auto Opts, class... Args>
         static void op(auto&& value, is_context auto&& ctx, Args&&... args) noexcept
         {
            dump<'{'>(std::forward<Args>(args)...);
            if (!value.empty()) {
               auto it = value.cbegin();
               auto write_pair = [&] {
                  using Key = decltype(it->first);
                  if constexpr (str_t<Key> || char_t<Key>) {
                     write<json>::op<Opts>(it->first, ctx, std::forward<Args>(args)...);
                  }
                  else {
                     dump<'"'>(std::forward<Args>(args)...);
                     write<json>::op<Opts>(it->first, ctx, std::forward<Args>(args)...);
                     dump<'"'>(std::forward<Args>(args)...);
                  }
                  dump<':'>(std::forward<Args>(args)...);
                  write<json>::op<Opts>(it->second, ctx, std::forward<Args>(args)...);
               };
               write_pair();
               ++it;
               
               const auto end = value.cend();
               for (; it != end; ++it) {
                  using Value = std::decay_t<decltype(it->second)>;
                  if constexpr (nullable_t<Value> && Opts.skip_null_members) {
                     if (!bool(it->second)) continue;
                  }
                  dump<','>(std::forward<Args>(args)...);
                  write_pair();
               }
            }
            dump<'}'>(std::forward<Args>(args)...);
         }
      };
      
      template <nullable_t T>
      struct to_json<T>
      {
         template <auto Opts, class... Args>
         static void op(auto&& value, is_context auto&& ctx, Args&&... args) noexcept
         {
            if (value)
               write<json>::op<Opts>(*value, ctx, std::forward<Args>(args)...);
            else {
               dump<"null">(std::forward<Args>(args)...);
            }
         }
      };

      template <class T>
      concept variant_t = is_specialization_v<T, std::variant>;

      template <variant_t T>
      struct to_json<T>
      {
         template <auto Opts, class... Args>
         static void op(auto&& value, is_context auto&& ctx, Args&&... args) noexcept
         {
            std::visit([&](auto&& val) { write<json>::op<Opts>(val, ctx, std::forward<Args>(args)...); }, value);
         }
      };

      template <class T>
      requires glaze_array_t<std::decay_t<T>> || tuple_t<std::decay_t<T>>
      struct to_json<T>
      {
         template <auto Opts, class... Args>
         static void op(auto&& value, is_context auto&& ctx, Args&&... args) noexcept
         {
            static constexpr auto N = []() constexpr
            {
               if constexpr (glaze_array_t<std::decay_t<T>>) {
                  return std::tuple_size_v<meta_t<std::decay_t<T>>>;
               }
               else {
                  return std::tuple_size_v<std::decay_t<T>>;
               }
            }
            ();
            
            dump<'['>(std::forward<Args>(args)...);
            using V = std::decay_t<T>;
            for_each<N>([&](auto I) {
               if constexpr (glaze_array_t<V>) {
                  write<json>::op<Opts>(value.*glz::tuplet::get<I>(meta_v<V>), ctx, std::forward<Args>(args)...);
               }
               else {
                  write<json>::op<Opts>(glz::tuplet::get<I>(value), ctx, std::forward<Args>(args)...);
               }
               // MSVC bug if this logic is in the `if constexpr`
               // https://developercommunity.visualstudio.com/t/stdc20-fatal-error-c1004-unexpected-end-of-file-fo/1509806
               constexpr bool needs_comma = I < N - 1;
               if constexpr (needs_comma) {
                  dump<','>(std::forward<Args>(args)...);
               }
            });
            dump<']'>(std::forward<Args>(args)...);
         }
      };
      
      template <class T>
      struct to_json<includer<T>>
      {
         template <auto Opts, class... Args>
         static void op(auto&& /*value*/, is_context auto&& /*ctx*/, Args&&... args) noexcept {
         }
      };      

      template <class T>
      requires is_std_tuple<std::decay_t<T>>
      struct to_json<T>
      {
         template <auto Opts, class... Args>
         static void op(auto&& value, is_context auto&& ctx, Args&&... args) noexcept
         {
            static constexpr auto N = []() constexpr
            {
               if constexpr (glaze_array_t<std::decay_t<T>>) {
                  return std::tuple_size_v<meta_t<std::decay_t<T>>>;
               }
               else {
                  return std::tuple_size_v<std::decay_t<T>>;
               }
            }
            ();

            dump<'['>(std::forward<Args>(args)...);
            using V = std::decay_t<T>;
            for_each<N>([&](auto I) {
               if constexpr (glaze_array_t<V>) {
                  write<json>::op<Opts>(value.*std::get<I>(meta_v<V>), ctx, std::forward<Args>(args)...);
               }
               else {
                  write<json>::op<Opts>(std::get<I>(value), ctx, std::forward<Args>(args)...);
               }
               // MSVC bug if this logic is in the `if constexpr`
               // https://developercommunity.visualstudio.com/t/stdc20-fatal-error-c1004-unexpected-end-of-file-fo/1509806
               constexpr bool needs_comma = I < N - 1;
               if constexpr (needs_comma) {
                  dump<','>(std::forward<Args>(args)...);
               }
            });
            dump<']'>(std::forward<Args>(args)...);
         }
      };

      template <const std::string_view& S>
      inline constexpr auto array_from_sv() noexcept
      {
         constexpr auto s = S; // Needed for MSVC to avoid an internal compiler error
         constexpr auto N = s.size();
         std::array<char, N> arr;
         std::copy_n(s.data(), N, arr.data());
         return arr;
      }

      inline constexpr bool needs_escaping(const auto& S) noexcept
      {
         for (const auto& c : S) {
            if (c == '"') {
               return true;
            }
         }
         return false;
      }
      
      template <class T>
      requires glaze_object_t<T>
      struct to_json<T>
      {
         template <auto Opts>
         static void op(auto&& value, is_context auto&& ctx, auto&& b) noexcept
         {
            using V = std::decay_t<T>;
            static constexpr auto N = std::tuple_size_v<meta_t<V>>;
            dump<'{'>(b);
            bool first = true;
            for_each<N>([&](auto I) {
               static constexpr auto item = glz::tuplet::get<I>(meta_v<V>);
               using mptr_t = std::tuple_element_t<1, decltype(item)>;
               using val_t = member_t<V, mptr_t>;

               if constexpr (nullable_t<val_t> && Opts.skip_null_members) {
                  auto is_null = [&]() {
                     if constexpr (std::is_member_pointer_v<std::tuple_element_t<1, decltype(item)>>) {
                        return !bool(value.*glz::tuplet::get<1>(item));
                     }
                     else {
                        return !bool(glz::tuplet::get<1>(item)(value));
                     }
                  }();
                  if (is_null) return;
               }

               if (first) {
                  first = false;
               }
               else {
                  // Null members may be skipped so we cant just write it out for all but the last member unless
                  // trailing commas are allowed
                  dump<','>(b);
               }

               using Key = typename std::decay_t<std::tuple_element_t<0, decltype(item)>>;
               if constexpr (str_t<Key> || char_t<Key>) {
                  write<json>::op<Opts>(glz::tuplet::get<0>(item), ctx, b);
                  dump<':'>(b);
               }
               else {
                  static constexpr auto quoted = concat_arrays(concat_arrays("\"", glz::tuplet::get<0>(item)), "\":");
                  write<json>::op<Opts>(quoted, ctx, b);
               }
               
               write<json>::op<Opts>(get_member(value, glz::tuplet::get<1>(item)), ctx, b);
               
               constexpr auto S = std::tuple_size_v<decltype(item)>;
               if constexpr (Opts.comments && S > 2) {
                  constexpr sv comment = glz::tuplet::get<2>(item);
                  if constexpr (comment.size() > 0) {
                     dump<"/*">(b);
                     dump(comment, b);
                     dump<"*/">(b);
                  }
               }
            });
            dump<'}'>(b);
         }
         
         template <auto Opts>
         static void op(auto&& value, is_context auto&& ctx, auto&& b, auto&& ix) noexcept
         {
            using V = std::decay_t<T>;
            static constexpr auto N = std::tuple_size_v<meta_t<V>>;
            dump<'{'>(b, ix);
            bool first = true;
            for_each<N>([&](auto I) {
               static constexpr auto item = glz::tuplet::get<I>(meta_v<V>);
               using mptr_t = std::tuple_element_t<1, decltype(item)>;
               using val_t = member_t<V, mptr_t>;

               if constexpr (nullable_t<val_t> && Opts.skip_null_members) {
                  auto is_null = [&]()
                  {
                     if constexpr (std::is_member_pointer_v<std::tuple_element_t<1, decltype(item)>>) {
                        return !bool(value.*glz::tuplet::get<1>(item));
                     }
                     else {
                        return !bool(glz::tuplet::get<1>(item)(value));
                     }
                  }();
                  if (is_null) return;
               }
               
               // skip file_include
               if constexpr (std::is_same_v<val_t, includer<std::decay_t<V>>>) {
                  return;
               }

               if (first) {
                  first = false;
               }
               else {
                  // Null members may be skipped so we cant just write it out for all but the last member unless
                  // trailing commas are allowed
                  dump<','>(b, ix);
               }

               using Key = typename std::decay_t<std::tuple_element_t<0, decltype(item)>>;
               
               if constexpr (str_t<Key> || char_t<Key>) {
                  static constexpr sv key = glz::tuplet::get<0>(item);
                  if constexpr (needs_escaping(key)) {
                     write<json>::op<Opts>(key, ctx, b, ix);
                     dump<':'>(b, ix);
                  }
                  else {
                     static constexpr auto quoted = join_v<chars<"\"">, key, chars<"\":">>;
                     dump<quoted>(b, ix);
                  }
               }
               else {
                  static constexpr auto quoted = concat_arrays(concat_arrays("\"", glz::tuplet::get<0>(item)), "\":");
                  write<json>::op<Opts>(quoted, ctx, b, ix);
               }
               
               write<json>::op<Opts>(get_member(value, glz::tuplet::get<1>(item)), ctx, b, ix);
               
               constexpr auto S = std::tuple_size_v<decltype(item)>;
               if constexpr (Opts.comments && S > 2) {
                  constexpr sv comment = glz::tuplet::get<2>(item);
                  if constexpr (comment.size() > 0) {
                     dump<"/*">(b, ix);
                     dump(comment, b, ix);
                     dump<"*/">(b, ix);
                  }
               }
            });
            dump<'}'>(b, ix);
         }
      };
   }  // namespace detail
   
   template <class T, class Buffer>
   inline auto write_json(T&& value, Buffer&& buffer) {
      return write<opts{}>(std::forward<T>(value), std::forward<Buffer>(buffer));
   }
   
   template <class T>
   inline auto write_json(T&& value) {
      std::string buffer{};
      write<opts{}>(std::forward<T>(value), buffer);
      return buffer;
   }
   
   template <class T, class Buffer>
   inline void write_jsonc(T&& value, Buffer&& buffer) {
      write<opts{.comments = true}>(std::forward<T>(value), std::forward<Buffer>(buffer));
   }
   
   template <class T>
   inline auto write_jsonc(T&& value) {
      std::string buffer{};
      write<opts{.comments = true}>(std::forward<T>(value), buffer);
      return buffer;
   }
   
   // std::string file_name needed for std::ofstream
   template <class T>
   inline void write_file_json(T&& value, const std::string& file_name) {
      
      std::string buffer{};
      
      write<opts{}>(std::forward<T>(value), buffer);
      
      std::ofstream file(file_name);
      
      if (file) {
         file << buffer;
      }
      else {
         throw std::runtime_error("could not write file: " + file_name);
      }
   }
}

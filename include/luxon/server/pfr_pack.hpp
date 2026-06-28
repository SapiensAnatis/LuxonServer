#include <string>
#include <string_view>
#include <type_traits>
#include <boost/pfr.hpp>

namespace server {
namespace pack_and_relink {
template <typename T> void string_views(T& instance, std::string& buffer) {
    // Recursive lambda to traverse fields
    auto visit_views = []<typename Self, typename U, typename Action>(this Self&& self, U& obj, Action&& action) {
        using CleanU = std::remove_cvref_t<U>;

        if constexpr (std::is_same_v<CleanU, std::string_view>) {
            // Found a string_view
            action(obj);
        } else if constexpr (std::is_array_v<CleanU>) {
            // Handle arrays of structs or arrays of string_views
            for (auto& element : obj)
                self(element, action);
        } else if constexpr (std::is_aggregate_v<CleanU>) {
            // Handle structs including nested structs and base classes
            boost::pfr::for_each_field(obj, [&](auto& field) { self(field, action); });
        }
    };

    // Determine total memory required
    std::size_t total_needed = 0;
    visit_views(instance, [&](const std::string_view& sv) { total_needed += sv.size(); });

    // Prepare continuous buffer
    buffer.reserve(total_needed);

    // Append data to continuous buffer and re-link views
    visit_views(instance, [&](std::string_view& sv) {
        if (sv.empty()) {
            // Handle empty views
            sv = "";
            return;
        }

        const std::size_t current_offset = buffer.size();
        buffer.append(sv.data(), sv.size());

        // Re-link string_view to continuous buffer
        sv = std::string_view(buffer.data() + current_offset, sv.size());
    });
}
} // namespace pack_and_relink
} // namespace server

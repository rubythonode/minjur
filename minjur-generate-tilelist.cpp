
#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <iostream>
#include <memory>
#include <set>
#include <string>

#include <osmium/index/map/all.hpp>
#include <osmium/handler/node_locations_for_ways.hpp>
#include <osmium/visitor.hpp>

#include <osmium/geom/tile.hpp>
#include <osmium/io/any_input.hpp>
#include <osmium/handler.hpp>
#include <osmium/osm.hpp>

using index_type = osmium::index::map::Map<osmium::unsigned_object_id_type, osmium::Location>;
using location_handler_type = osmium::handler::NodeLocationsForWays<index_type>;

class TileDiffHandler : public osmium::handler::Handler {

    int m_zoom;
    index_type& m_old_index;
    index_type& m_tmp_index;

    std::set<osmium::geom::Tile> m_dirty_tiles;

    void add_location(const osmium::Location& location) {
        if (location.valid()) {
            m_dirty_tiles.emplace(m_zoom, location);
        }
    }

public:

    TileDiffHandler(int zoom, index_type& old_index, index_type& tmp_index) :
        m_zoom(zoom),
        m_old_index(old_index),
        m_tmp_index(tmp_index) {
    }

    void node(const osmium::Node& node) {
        try {
            add_location(m_old_index.get(node.id()));
        } catch (...) {
        }
        try {
            add_location(node.location());
        } catch (...) {
        }
    }

    void way(const osmium::Way& way) {
        for (const auto& node_ref : way.nodes()) {
            try {
                add_location(m_old_index.get(node_ref.ref()));
            } catch (...) {
            }
            try {
                add_location(m_tmp_index.get(node_ref.ref()));
            } catch (...) {
            }
        }
    }

    void dump_tiles() const {
        for (const auto& tile : m_dirty_tiles) {
            std::cout << tile.z << " " << tile.x << " " << tile.y << "\n";
        }
    }

}; // class TileDiffHandler

void print_help() {
    std::cout << "minjur-generate-tilelist [OPTIONS] OSM-CHANGE-FILE\n\n" \
              << "Output is always to stdout.\n" \
              << "\nOptions:\n" \
              << "  -h, --help                 This help message\n" \
              << "  -l, --location_store=TYPE  Set location store\n" \
              << "  -L, --list-location-stores Show available location stores\n" \
              << "  -n, --nodes=sparse|dense   Are node IDs sparse or dense?\n" \
              << "  -z, --zoom=ZOOM            Zoom level for tiles (default: 15)\n";
}

int main(int argc, char* argv[]) {
    const auto& map_factory = osmium::index::MapFactory<osmium::unsigned_object_id_type, osmium::Location>::instance();

    static struct option long_options[] = {
        {"help",                       no_argument, 0, 'h'},
        {"location_store",       required_argument, 0, 'l'},
        {"list_location_stores",       no_argument, 0, 'L'},
        {"nodes",                required_argument, 0, 'n'},
        {"zoom",                 required_argument, 0, 'z'},
        {0, 0, 0, 0}
    };

    std::string input_filename = "-";
    std::string location_store = "sparse_file_array,locations.dump";
    std::string locations_dump_file;
    bool nodes_dense = false;
    int zoom = 15;

    while (true) {
        int c = getopt_long(argc, argv, "hl:Ln:z", long_options, 0);
        if (c == -1) {
            break;
        }

        switch (c) {
            case 'h':
                print_help();
                std::exit(0);
            case 'l':
                location_store = optarg;
                break;
            case 'L':
                std::cout << "Available map types:\n";
                for (const auto& map_type : map_factory.map_types()) {
                    std::cout << "  " << map_type << "\n";
                }
                std::exit(0);
            case 'n':
                if (!std::strcmp(optarg, "sparse")) {
                    nodes_dense = false;
                } else if (!std::strcmp(optarg, "dense")) {
                    nodes_dense = true;
                } else {
                    std::cerr << "Set --nodes, -n to 'sparse' or 'dense'\n";
                    std::exit(1);
                }
                break;
            case 'z':
                zoom = std::atoi(optarg);
                break;
            default:
                std::exit(1);
        }
    }

    if (location_store.empty()) {
        location_store = nodes_dense ? "dense" : "sparse";
        location_store.append("_file_array,locations.dump");
    }

    std::cerr << "Using the '" << location_store << "' location store. Use -l or -n to change this.\n";

    const int remaining_args = argc - optind;
    if (remaining_args > 1) {
        std::cerr << "Usage: " << argv[0] << " [OPTIONS] OSM-CHANGE-FILE\n";
        std::exit(1);
    } else if (remaining_args == 1) {
        input_filename = argv[optind];
        std::cerr << "Reading from '" << input_filename << "'...\n";
    } else {
        input_filename = "-";
        std::cerr << "Reading from STDIN...\n";
    }

    osmium::io::Reader reader{input_filename};

    std::unique_ptr<index_type> old_index = map_factory.create_map(location_store);
    std::unique_ptr<index_type> tmp_index = map_factory.create_map("sparse_mem_array");
    location_handler_type location_handler{*tmp_index};
    location_handler.ignore_errors();

    TileDiffHandler tile_diff_handler{zoom, *old_index, *tmp_index};

    osmium::apply(reader, location_handler, tile_diff_handler);
    reader.close();

    tile_diff_handler.dump_tiles();
}


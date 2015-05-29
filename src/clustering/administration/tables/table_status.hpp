// Copyright 2010-2014 RethinkDB, all rights reserved.
#ifndef CLUSTERING_ADMINISTRATION_TABLES_TABLE_STATUS_HPP_
#define CLUSTERING_ADMINISTRATION_TABLES_TABLE_STATUS_HPP_

#include <string>

#include "errors.hpp"
#include <boost/shared_ptr.hpp>

#include "clustering/administration/tables/calculate_status.hpp"
#include "clustering/administration/tables/table_common.hpp"

class server_config_client_t;
class table_status_artificial_table_backend_t;

/* Utility function to wait for table readiness through the `table_status` backend. Note
that even if this function indicates that the table is ready, an attempt to write to it
may fail, because this server's `cluster_namespace_interface_t` might not be ready yet.
If you need that, call `namespace_interface_t::check_readiness()` after calling this. */
enum class table_wait_result_t {
    WAITED,    // The table is ready after waiting for it
    IMMEDIATE, // The table was already ready
    DELETED,   // The table has been deleted
};

table_wait_result_t wait_for_table_readiness(
    const namespace_id_t &table_id,
    table_readiness_t readiness,
    const table_status_artificial_table_backend_t *table_status_backend,
    signal_t *interruptor,
    ql::datum_t *status_out  /* can be null */)
    THROWS_ONLY(interrupted_exc_t);

ql::datum_t convert_table_status_to_datum(
        const namespace_id_t &table_id,
        const name_string_t &table_name,
        const ql::datum_t &db_name_or_uuid,
        table_readiness_t readiness,
        const std::vector<shard_status_t> &shard_statuses,
        admin_identifier_format_t identifier_format,
        const server_name_map_t &server_names);

class table_status_artificial_table_backend_t :
    public common_table_artificial_table_backend_t
{
public:
    table_status_artificial_table_backend_t(
            boost::shared_ptr<semilattice_readwrite_view_t<
                cluster_semilattice_metadata_t> > _semilattice_view,
            table_meta_client_t *_table_meta_client,
            admin_identifier_format_t _identifier_format,
            server_config_client_t *_server_config_client);
    ~table_status_artificial_table_backend_t();

    bool write_row(
            ql::datum_t primary_key,
            bool pkey_was_autogenerated,
            ql::datum_t *new_value_inout,
            signal_t *interruptor_on_caller,
            std::string *error_out);

private:
    void format_row(
            const namespace_id_t &table_id,
            const table_basic_config_t &basic_config,
            const ql::datum_t &db_name_or_uuid,
            signal_t *interruptor_on_home,
            ql::datum_t *row_out)
            THROWS_ONLY(interrupted_exc_t, no_such_table_exc_t, failed_table_op_exc_t,
                admin_op_exc_t);

    friend table_wait_result_t wait_for_table_readiness(
        const namespace_id_t &,
        table_readiness_t,
        const table_status_artificial_table_backend_t *,
        signal_t *,
        ql::datum_t *)
        THROWS_ONLY(interrupted_exc_t);

    server_config_client_t *server_config_client;
};

#endif /* CLUSTERING_ADMINISTRATION_TABLES_TABLE_STATUS_HPP_ */


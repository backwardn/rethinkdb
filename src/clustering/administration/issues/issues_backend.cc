// Copyright 2010-2014 RethinkDB, all rights reserved.
#include "clustering/administration/issues/issues_backend.hpp"

#include "clustering/administration/datum_adapter.hpp"
#include "clustering/administration/main/watchable_fields.hpp"
#include "concurrency/cross_thread_signal.hpp"

issues_artificial_table_backend_t::issues_artificial_table_backend_t(
        boost::shared_ptr<semilattice_read_view_t<cluster_semilattice_metadata_t> >
            _cluster_sl_view,
        const clone_ptr_t<watchable_t<change_tracking_map_t<peer_id_t,
            cluster_directory_metadata_t> > >
                &directory_view,
        server_config_client_t *_server_config_client,
        table_meta_client_t *_table_meta_client,
        admin_identifier_format_t _identifier_format) :
    identifier_format(_identifier_format),
    cluster_sl_view(_cluster_sl_view),
    server_config_client(_server_config_client),
    table_meta_client(_table_meta_client),
    remote_issue_tracker(
        directory_view->incremental_subview(
            incremental_field_getter_t<local_issues_t,
                                       cluster_directory_metadata_t>(
                &cluster_directory_metadata_t::local_issues)),
        directory_view->incremental_subview(
            incremental_field_getter_t<server_id_t,
                                       cluster_directory_metadata_t>(
                &cluster_directory_metadata_t::server_id))),
    name_collision_issue_tracker(
        server_config_client, cluster_sl_view, table_meta_client),
    table_issue_tracker(server_config_client, table_meta_client)
{
    trackers.insert(&remote_issue_tracker);
    trackers.insert(&name_collision_issue_tracker);
    trackers.insert(&table_issue_tracker);
}

issues_artificial_table_backend_t::~issues_artificial_table_backend_t() {
    begin_changefeed_destruction();
}

std::string issues_artificial_table_backend_t::get_primary_key_name() {
    return "id";
}

bool issues_artificial_table_backend_t::read_all_rows_as_vector(
        signal_t *interruptor,
        std::vector<ql::datum_t> *rows_out,
        UNUSED std::string *error_out) {
    cross_thread_signal_t ct_interruptor(interruptor, home_thread());
    on_thread_t rethreader(home_thread());
    rows_out->clear();

    cluster_semilattice_metadata_t metadata = cluster_sl_view->get();
    for (auto const &tracker : trackers) {
        for (auto const &issue : tracker->get_issues(&ct_interruptor)) {
            ql::datum_t row;
            bool still_valid = issue->to_datum(cluster_sl_view->get(),
                server_config_client, table_meta_client, identifier_format, &row);
            if (!still_valid) {
                /* Based on `metadata`, the issue decided it is no longer relevant. */
                continue;
            }
            rows_out->push_back(row);
        }
    }

    return true;
}

bool issues_artificial_table_backend_t::read_row(ql::datum_t primary_key,
                                                 signal_t *interruptor,
                                                 ql::datum_t *row_out,
                                                 UNUSED std::string *error_out) {
    cross_thread_signal_t ct_interruptor(interruptor, home_thread());
    on_thread_t rethreader(home_thread());
    *row_out = ql::datum_t();

    uuid_u issue_id;
    std::string dummy_error;
    if (convert_uuid_from_datum(primary_key, &issue_id, &dummy_error)) {
        std::vector<scoped_ptr_t<issue_t> > issues = all_issues(&ct_interruptor);

        for (auto const &issue : issues) {
            if (issue->get_id() == issue_id) {
                ql::datum_t row;
                bool still_valid = issue->to_datum(cluster_sl_view->get(),
                    server_config_client, table_meta_client, identifier_format, &row);
                if (still_valid) {
                    *row_out = row;
                }
                break;
            }
        }
    }
    return true;
}

std::vector<scoped_ptr_t<issue_t> > issues_artificial_table_backend_t::all_issues(
        signal_t *interruptor) const {
    std::vector<scoped_ptr_t<issue_t> > all_issues;

    for (auto const &tracker : trackers) {
        std::vector<scoped_ptr_t<issue_t> > issues = tracker->get_issues(interruptor);
        std::move(issues.begin(), issues.end(), std::back_inserter(all_issues));
    }

    return all_issues;
}

bool issues_artificial_table_backend_t::write_row(UNUSED ql::datum_t primary_key,
                                                  UNUSED bool pkey_was_autogenerated,
                                                  UNUSED ql::datum_t *new_value_inout,
                                                  UNUSED signal_t *interruptor,
                                                  std::string *error_out) {
    error_out->assign("It's illegal to write to the `rethinkdb.current_issues` table.");
    return false;
}


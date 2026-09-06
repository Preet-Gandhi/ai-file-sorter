#pragma once

#include "Types.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <mutex>

struct sqlite3;

/**
 * @brief Persistent user-owned learning data, separate from disposable caches.
 */
class UserLearningStore {
public:
    /**
     * @brief User-approved categorization decision captured from the review flow.
     */
    struct ApprovedMapping {
        /** @brief Original file name associated with the approved decision. */
        std::string file_name;
        /** @brief Type of filesystem entry the approval applies to. */
        FileType file_type{FileType::File};
        /** @brief Directory containing the approved file. */
        std::string dir_path;
        /** @brief Canonical category approved by the user. */
        std::string category;
        /** @brief Canonical subcategory approved by the user. */
        std::string subcategory;
        /** @brief Suggested renamed file name, when available. */
        std::string suggested_name;
        /** @brief Optional text context reserved for future embedding/retrieval use. */
        std::string context_text;
        /** @brief Whether consistency hints influenced the original model suggestion. */
        bool used_consistency_hints{false};
        /** @brief Source tag describing where the approval came from. */
        std::string source{"review_confirmed"};
    };

    /**
     * @brief Learned taxonomy entry derived from user-approved decisions.
     */
    struct TaxonomyEntry {
        /** @brief Stable row id in the learned taxonomy database. */
        int id{0};
        /** @brief Canonical learned category label. */
        std::string category;
        /** @brief Canonical learned subcategory label. */
        std::string subcategory;
        /** @brief Number of approved examples currently attached to this entry. */
        int example_count{0};
        /** @brief Source tag for this taxonomy entry. */
        std::string source;
    };

    /**
     * @brief User-owned taxonomy candidate imported from a non-cache source.
     */
    struct TaxonomyCandidate {
        /** @brief Category label to make available to learned categorization. */
        std::string category;
        /** @brief Optional subcategory label paired with the category. */
        std::string subcategory;
        /** @brief Source tag, for example `whitelist:Default`. */
        std::string source{"user_taxonomy"};
    };

    /**
     * @brief Ranked taxonomy candidate retrieved from learned user data.
     */
    struct RetrievedCandidate {
        /** @brief Stable learned taxonomy row id. */
        int taxonomy_entry_id{0};
        /** @brief Candidate category label. */
        std::string category;
        /** @brief Candidate subcategory label, when available. */
        std::string subcategory;
        /** @brief Number of approved examples attached to the candidate. */
        int example_count{0};
        /** @brief Source tag for this candidate. */
        std::string source;
        /** @brief Lexical relevance score for the query. */
        int score{0};
        /** @brief Cosine similarity from stored embeddings when available. */
        double embedding_similarity{0.0};
        /** @brief True when embedding similarity contributed to the ranking. */
        bool used_embedding{false};
    };

    /**
     * @brief Stored embedding vector for a learned taxonomy entry.
     */
    struct TaxonomyEmbedding {
        /** @brief Learned taxonomy row represented by the vector. */
        int taxonomy_entry_id{0};
        /** @brief Embedding model/version that produced the vector. */
        std::string embedding_model;
        /** @brief Stable hash of the source text embedded for this row. */
        std::string source_text_hash;
        /** @brief Number of float dimensions stored in the vector. */
        int dimension{0};
        /** @brief Embedding vector values. */
        std::vector<float> vector;
    };

    /**
     * @brief Stored approved example linked to a learned taxonomy entry.
     */
    struct ApprovedExample {
        /** @brief Stable row id for the approved example. */
        int id{0};
        /** @brief Learned taxonomy row this example supports. */
        int taxonomy_entry_id{0};
        /** @brief Original file name associated with the approval. */
        std::string file_name;
        /** @brief Type of filesystem entry represented by the approval. */
        FileType file_type{FileType::File};
        /** @brief Directory containing the approved file. */
        std::string dir_path;
        /** @brief Approved category label. */
        std::string category;
        /** @brief Approved subcategory label. */
        std::string subcategory;
        /** @brief Suggested renamed file name, when available. */
        std::string suggested_name;
        /** @brief Optional text context reserved for future embedding/retrieval use. */
        std::string context_text;
        /** @brief Whether consistency hints influenced the original model suggestion. */
        bool used_consistency_hints{false};
        /** @brief Source tag describing where this example came from. */
        std::string source;
    };

    /**
     * @brief Open or create the user-learning database under the app config directory.
     * @param config_dir Application configuration directory.
     */
    explicit UserLearningStore(std::string config_dir);
    /**
     * @brief Close the user-learning database connection.
     */
    ~UserLearningStore();

    UserLearningStore(const UserLearningStore&) = delete;
    UserLearningStore& operator=(const UserLearningStore&) = delete;

    /**
     * @brief Return whether the SQLite database connection is open.
     * @return True when the store can persist learning data.
     */
    bool is_open() const { return db_ != nullptr; }
    /**
     * @brief Return the filesystem path to the learning database.
     * @return Absolute path to `user_learning.db`.
     */
    const std::filesystem::path& database_path() const { return db_file_; }

    /**
     * @brief Persist a user-approved categorization mapping.
     * @param mapping Approved decision to store.
     * @param error Optional output for a human-readable failure reason.
     * @return True when the mapping was stored or updated successfully.
     */
    bool record_approved_mapping(const ApprovedMapping& mapping, std::string* error = nullptr);
    /**
     * @brief Persist a user-owned taxonomy candidate without recording a file example.
     * @param candidate Candidate category/subcategory to make available for retrieval.
     * @param error Optional output for a human-readable failure reason.
     * @return True when the candidate was stored or already existed.
     */
    bool record_taxonomy_candidate(const TaxonomyCandidate& candidate, std::string* error = nullptr);
    /**
     * @brief Import multiple user-owned taxonomy candidates in one transaction.
     * @param candidates Candidate category/subcategory labels to import.
     * @param error Optional output for a human-readable failure reason.
     * @return True when all non-empty candidates were imported successfully.
     */
    bool import_taxonomy_candidates(const std::vector<TaxonomyCandidate>& candidates,
                                    std::string* error = nullptr);
    /**
     * @brief Remove non-approved taxonomy candidates whose source starts with the given prefix.
     * @param source_prefix Prefix to match, for example `whitelist:`.
     * @param error Optional output for a human-readable failure reason.
     * @return True when matching imported candidates were removed successfully.
     */
    bool remove_taxonomy_candidates_with_source_prefix(const std::string& source_prefix,
                                                       std::string* error = nullptr);
    /**
     * @brief Find a learned taxonomy entry by category/subcategory labels.
     * @param category Category label to resolve.
     * @param subcategory Subcategory label to resolve.
     * @return Matching learned taxonomy entry, if present.
     */
    std::optional<TaxonomyEntry> find_taxonomy_entry(const std::string& category,
                                                     const std::string& subcategory) const;
    /**
     * @brief Retrieve learned taxonomy candidates relevant to file context text.
     * @param query_text Filename, path, summary, image description, or other categorization context.
     * @param limit Maximum number of candidates to return.
     * @return Ranked candidates with positive relevance scores.
     */
    std::vector<RetrievedCandidate> retrieve_taxonomy_candidates(const std::string& query_text,
                                                                 std::size_t limit = 5) const;
    /**
     * @brief Rebuild persisted embedding vectors for all learned taxonomy entries.
     * @param error Optional output for a human-readable failure reason.
     * @return True when all current entries have refreshed vectors.
     */
    bool rebuild_taxonomy_embeddings(std::string* error = nullptr);
    /**
     * @brief Remove all learned taxonomy, examples, aliases, and embeddings.
     * @param error Optional output for a human-readable failure reason.
     * @return True when learned behavior was reset successfully.
     */
    bool clear_all(std::string* error = nullptr);
    /**
     * @brief Load a stored taxonomy embedding for inspection or tests.
     * @param taxonomy_entry_id Learned taxonomy row id.
     * @param embedding_model Model/version id to read, or the active local model when empty.
     * @return Stored embedding record, if present.
     */
    std::optional<TaxonomyEmbedding> taxonomy_embedding(int taxonomy_entry_id,
                                                        const std::string& embedding_model = {}) const;
    /**
     * @brief Count stored taxonomy embeddings for the active local model.
     * @return Number of persisted embedding rows.
     */
    int taxonomy_embedding_count() const;
    /**
     * @brief Load all learned taxonomy entries currently stored.
     * @return Taxonomy entries ordered by label for stable inspection.
     */
    std::vector<TaxonomyEntry> taxonomy_entries() const;
    /**
     * @brief Count learned taxonomy entries currently stored.
     * @return Number of taxonomy entry rows.
     */
    int taxonomy_entry_count() const;
    /**
     * @brief Load all approved examples currently stored in the learning database.
     * @return Approved examples in database order.
     */
    std::vector<ApprovedExample> approved_examples() const;
    /**
     * @brief Count approved examples stored in the learning database.
     * @return Number of approved example rows.
     */
    int approved_example_count() const;

    /**
     * @brief Build the learning database path for a configuration directory.
     * @param config_dir Application configuration directory.
     * @return Path to the separate user-learning database.
     */
    static std::filesystem::path database_path_for_config_dir(const std::string& config_dir);

private:
    /**
     * @brief Create or migrate the learning database schema.
     * @param error Optional output for a human-readable failure reason.
     * @return True when the schema is ready.
     */
    bool initialize_schema(std::string* error);
    /**
     * @brief Resolve or create the learned taxonomy entry for a mapping.
     * @param mapping Approved mapping whose labels should be resolved.
     * @param error Optional output for a human-readable failure reason.
     * @return Learned taxonomy id, or a negative value on failure.
     */
    int resolve_taxonomy_entry(const ApprovedMapping& mapping, std::string* error);
    /**
     * @brief Resolve or create a taxonomy entry from raw labels.
     * @param category Raw category label.
     * @param subcategory Raw subcategory label.
     * @param source Source tag to store when creating or source-updating the entry.
     * @param preserve_existing_source True to avoid overwriting an existing source tag.
     * @param error Optional output for a human-readable failure reason.
     * @return Learned taxonomy id, or a negative value on failure.
     */
    int resolve_taxonomy_entry(const std::string& category,
                               const std::string& subcategory,
                               const std::string& source,
                               bool preserve_existing_source,
                               std::string* error);
    /**
     * @brief Insert or update the approved example linked to a taxonomy entry.
     * @param mapping Approved mapping to store.
     * @param taxonomy_entry_id Current learned taxonomy id.
     * @param old_taxonomy_entry_id Previously linked taxonomy id, if the example is being moved.
     * @param error Optional output for a human-readable failure reason.
     * @return True when the example was stored successfully.
     */
    bool upsert_approved_example(const ApprovedMapping& mapping,
                                 int taxonomy_entry_id,
                                 std::optional<int> old_taxonomy_entry_id,
                                 std::string* error);
    /**
     * @brief Look up the existing taxonomy id for the same approved example key.
     * @param mapping Mapping whose file key should be checked.
     * @return Previously linked taxonomy id, if any.
     */
    std::optional<int> existing_example_taxonomy_id(const ApprovedMapping& mapping) const;
    /**
     * @brief Recompute an entry's approved-example count from stored examples.
     * @param taxonomy_entry_id Learned taxonomy entry to refresh.
     * @param error Optional output for a human-readable failure reason.
     * @return True when the count was updated.
     */
    bool refresh_example_count(int taxonomy_entry_id, std::string* error);
    /**
     * @brief Rebuild the persisted embedding for one taxonomy entry from labels and examples.
     * @param taxonomy_entry_id Learned taxonomy row id.
     * @param error Optional output for a human-readable failure reason.
     * @return True when the embedding was refreshed or the row no longer exists.
     */
    bool refresh_taxonomy_embedding(int taxonomy_entry_id, std::string* error);
    /**
     * @brief Build deterministic embedding source text for one learned taxonomy row.
     * @param taxonomy_entry_id Learned taxonomy row id.
     * @return Source text containing labels and approved example context.
     */
    std::string embedding_source_text_for_taxonomy_entry(int taxonomy_entry_id) const;
    /**
     * @brief Normalize a category label for stable learned-taxonomy lookups.
     * @param value Raw label text.
     * @return Normalized lookup key.
     */
    std::string normalize_label(const std::string& value) const;

    sqlite3* db_{nullptr};
    mutable std::recursive_mutex mutex_;
    std::filesystem::path db_file_;
};

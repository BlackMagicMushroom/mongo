/**
 * Copyright (C) 2016 MongoDB Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the copyright holders give permission to link the
 * code of portions of this program with the OpenSSL library under certain
 * conditions as described in each individual source file and distribute
 * linked combinations including the program with the OpenSSL library. You
 * must comply with the GNU Affero General Public License in all respects
 * for all of the code used other than as permitted herein. If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version. If you
 * delete this exception statement from all source files in the program,
 * then also delete it in the license file.
 */

#pragma once

#include <deque>

#include "mongo/db/namespace_string.h"
#include "mongo/db/pipeline/document_source.h"
#include "mongo/db/pipeline/document_source_limit.h"
#include "mongo/db/query/plan_summary_stats.h"
#include "mongo/db/range_preserver.h"

namespace mongo {

class PlanExecutor;

/**
 * Constructs and returns Documents from the BSONObj objects produced by a supplied PlanExecutor.
 */
class DocumentSourceCursor final : public DocumentSource {
public:
    // virtuals from DocumentSource
    GetNextResult getNext() final;
    const char* getSourceName() const final;
    BSONObjSet getOutputSorts() final {
        return _outputSorts;
    }
    Value serialize(boost::optional<ExplainOptions::Verbosity> explain = boost::none) const final;
    bool isValidInitialSource() const final {
        return true;
    }

    void detachFromOperationContext() final;

    void reattachToOperationContext(OperationContext* opCtx) final;

    /**
     * Create a document source based on a passed-in PlanExecutor. 'exec' must be a yielding
     * PlanExecutor, and must be registered with the associated collection's CursorManager.
     */
    static boost::intrusive_ptr<DocumentSourceCursor> create(
        Collection* collection,
        std::unique_ptr<PlanExecutor, PlanExecutor::Deleter> exec,
        const boost::intrusive_ptr<ExpressionContext>& pExpCtx);

    /*
      Record the query that was specified for the cursor this wraps, if
      any.

      This should be captured after any optimizations are applied to
      the pipeline so that it reflects what is really used.

      This gets used for explain output.

      @param pBsonObj the query to record
     */
    void setQuery(const BSONObj& query) {
        _query = query;
    }

    /*
      Record the sort that was specified for the cursor this wraps, if
      any.

      This should be captured after any optimizations are applied to
      the pipeline so that it reflects what is really used.

      This gets used for explain output.

      @param pBsonObj the sort to record
     */
    void setSort(const BSONObj& sort) {
        _sort = sort;
    }

    /**
     * Informs this object of projection and dependency information.
     *
     * @param projection The projection that has been passed down to the query system.
     * @param deps The output of DepsTracker::toParsedDeps.
     */
    void setProjection(const BSONObj& projection, const boost::optional<ParsedDeps>& deps) {
        _projection = projection;
        _dependencies = deps;
    }

    /**
     * Returns the limit associated with this cursor, or -1 if there is no limit.
     */
    long long getLimit() const {
        return _limit ? _limit->getLimit() : -1;
    }

    /**
     * If subsequent sources need no information from the cursor, the cursor can simply output empty
     * documents, avoiding the overhead of converting BSONObjs to Documents.
     */
    void shouldProduceEmptyDocs() {
        _shouldProduceEmptyDocs = true;
    }

    const std::string& getPlanSummaryStr() const {
        return _planSummary;
    }

    const PlanSummaryStats& getPlanSummaryStats() const {
        return _planSummaryStats;
    }

protected:
    /**
     * Disposes of '_exec' and '_rangePreserver' if they haven't been disposed already. This
     * involves taking a collection lock.
     */
    void doDispose() final;

    /**
     * Attempts to combine with any subsequent $limit stages by setting the internal '_limit' field.
     */
    Pipeline::SourceContainer::iterator doOptimizeAt(Pipeline::SourceContainer::iterator itr,
                                                     Pipeline::SourceContainer* container) final;

private:
    DocumentSourceCursor(Collection* collection,
                         std::unique_ptr<PlanExecutor, PlanExecutor::Deleter> exec,
                         const boost::intrusive_ptr<ExpressionContext>& pExpCtx);
    ~DocumentSourceCursor();

    /**
     * Acquires locks to properly destroy and de-register '_exec'. '_exec' must be non-null.
     */
    void cleanupExecutor();

    /**
     * Reads a batch of data from '_exec'.
     */
    void loadBatch();

    void recordPlanSummaryStats();

    std::deque<Document> _currentBatch;

    // BSONObj members must outlive _projection and cursor.
    BSONObj _query;
    BSONObj _sort;
    BSONObj _projection;
    bool _shouldProduceEmptyDocs = false;
    boost::optional<ParsedDeps> _dependencies;
    boost::intrusive_ptr<DocumentSourceLimit> _limit;
    long long _docsAddedToBatches;  // for _limit enforcement

    // Both '_rangePreserver' and '_exec' must be destroyed while holding the collection lock.
    RangePreserver _rangePreserver;
    std::unique_ptr<PlanExecutor, PlanExecutor::Deleter> _exec;

    BSONObjSet _outputSorts;
    std::string _planSummary;
    PlanSummaryStats _planSummaryStats;
};

}  // namespace mongo
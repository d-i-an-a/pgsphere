#include "postgres.h"
#include "optimizer/paths.h"
#include "optimizer/pathnode.h"
#include "optimizer/restrictinfo.h"
#include "utils/builtins.h"
#include "utils/elog.h"
#include "utils/lsyscache.h"
#include "nodes/print.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_operator.h"
#include "commands/explain.h"
#include "funcapi.h"

#include "access/htup_details.h"
#include "access/heapam.h"

#include "point.h"
#include "crossmatch.h"

extern void _PG_init(void);

static set_join_pathlist_hook_type set_join_pathlist_next;

typedef struct
{
	CustomPath		cpath;

	JoinType		jointype;

	Path		   *outer_path;
	Path		   *inner_path;

	List		   *joinrestrictinfo;
} CrossmatchJoinPath;

typedef struct
{
	CustomScanState css;

	HeapTupleData	scan_tuple;		/* buffer to fetch tuple */
	List		   *dev_tlist;		/* tlist to be returned from the device */
	List		   *dev_quals;		/* quals to be run on the device */

	Relation		left;
	Relation		right;
} CrossmatchScanState;

static CustomPathMethods	crossmatch_path_methods;
static CustomScanMethods	crossmatch_plan_methods;
static CustomExecMethods	crossmatch_exec_methods;


static Path *
crossmatch_find_cheapest_path(PlannerInfo *root,
							  RelOptInfo *joinrel,
							  RelOptInfo *inputrel)
{
	Path	   *input_path = inputrel->cheapest_total_path;
	Relids		other_relids;
	ListCell   *lc;

	other_relids = bms_difference(joinrel->relids, inputrel->relids);
	if (bms_overlap(PATH_REQ_OUTER(input_path), other_relids))
	{
		input_path = NULL;
		foreach (lc, inputrel->pathlist)
		{
			Path   *curr_path = lfirst(lc);

			if (bms_overlap(PATH_REQ_OUTER(curr_path), other_relids))
				continue;
			if (input_path == NULL ||
				input_path->total_cost > curr_path->total_cost)
				input_path = curr_path;
		}
	}
	bms_free(other_relids);

	return input_path;
}

static void
create_crossmatch_path(PlannerInfo *root,
					   RelOptInfo *joinrel,
					   Path *outer_path,
					   Path *inner_path,
					   ParamPathInfo *param_info,
					   List *restrict_clauses,
					   Relids required_outer)
{
	CrossmatchJoinPath *result;

	result = palloc0(sizeof(CrossmatchJoinPath));
	NodeSetTag(result, T_CustomPath);

	result->cpath.path.pathtype = T_CustomScan;
	result->cpath.path.parent = joinrel;
	result->cpath.path.param_info = param_info;
	result->cpath.path.pathkeys = NIL;
	result->cpath.path.pathtarget = &joinrel->reltarget;
	result->cpath.path.rows = joinrel->rows;
	result->cpath.flags = 0;
	result->cpath.methods = &crossmatch_path_methods;
	result->outer_path = outer_path;
	result->inner_path = inner_path;
	result->joinrestrictinfo = restrict_clauses;

	/* TODO: real costs */
	result->cpath.path.startup_cost = 1;
	result->cpath.path.total_cost = 1;

	add_path(joinrel, &result->cpath.path);
}

static void
join_pathlist_hook(PlannerInfo *root,
				   RelOptInfo *joinrel,
				   RelOptInfo *outerrel,
				   RelOptInfo *innerrel,
				   JoinType jointype,
				   JoinPathExtraData *extra)
{
	ListCell   *restr;
	text	   *dist_func_name = cstring_to_text("dist(spoint,spoint)");
	Oid			dist_func;
	List	   *restrict_clauses = extra->restrictlist;
	Relids		required_relids = NULL;

	if (outerrel->reloptkind == RELOPT_BASEREL &&
		innerrel->reloptkind == RELOPT_BASEREL)
	{
		required_relids = bms_add_member(required_relids, outerrel->relid);
		required_relids = bms_add_member(required_relids, innerrel->relid);
	}
	else return; /* one of relations can't have index */

	dist_func = DatumGetObjectId(DirectFunctionCall1(to_regprocedure,
													 PointerGetDatum(dist_func_name)));

	if (dist_func == InvalidOid)
		elog(ERROR, "function dist not found!");

	if (set_join_pathlist_next)
		set_join_pathlist_next(root,
							   joinrel,
							   outerrel,
							   innerrel,
							   jointype,
							   extra);

	foreach(restr, extra->restrictlist)
	{
		RestrictInfo *restrInfo = (RestrictInfo *) lfirst(restr);

		/* Skip irrelevant JOIN case */
		if (!bms_equal(required_relids, restrInfo->required_relids))
			continue;

		if (IsA(restrInfo->clause, OpExpr))
		{
			OpExpr	   *opExpr = (OpExpr *) restrInfo->clause;
			int			nargs = list_length(opExpr->args);
			Node	   *arg1;
			Node	   *arg2;

			if (nargs != 2)
				continue;

			arg1 = linitial(opExpr->args);
			arg2 = lsecond(opExpr->args);

			if (opExpr->opno == Float8LessOperator &&
				IsA(arg1, FuncExpr) &&
				((FuncExpr *) arg1)->funcid == dist_func &&
				IsA(arg2, Const))
			{
				Path *outer_path = crossmatch_find_cheapest_path(root, joinrel, outerrel);
				Path *inner_path = crossmatch_find_cheapest_path(root, joinrel, innerrel);

				Relids required_outer = calc_nestloop_required_outer(outer_path, inner_path);

				ParamPathInfo *param_info = get_joinrel_parampathinfo(root,
																	  joinrel,
																	  outer_path,
																	  inner_path,
																	  extra->sjinfo,
																	  required_outer,
																	  &restrict_clauses);

				create_crossmatch_path(root, joinrel, outer_path, inner_path,
									   param_info, restrict_clauses, required_outer);

				break;
			}
			else if (opExpr->opno == get_commutator(Float8LessOperator) &&
					 IsA(arg1, Const) &&
					 IsA(arg2, FuncExpr) &&
					 ((FuncExpr *) arg2)->funcid == dist_func)
			{
				/* TODO: merge duplicate code */
				Path *outer_path = crossmatch_find_cheapest_path(root, joinrel, outerrel);
				Path *inner_path = crossmatch_find_cheapest_path(root, joinrel, innerrel);

				Relids required_outer = calc_nestloop_required_outer(outer_path, inner_path);

				ParamPathInfo *param_info = get_joinrel_parampathinfo(root,
																	  joinrel,
																	  outer_path,
																	  inner_path,
																	  extra->sjinfo,
																	  required_outer,
																	  &restrict_clauses);

				create_crossmatch_path(root, joinrel, outer_path, inner_path,
									   param_info, restrict_clauses, required_outer);
			}
		}
	}
}

static Plan *
create_crossmatch_plan(PlannerInfo *root,
					   RelOptInfo *rel,
					   CustomPath *best_path,
					   List *tlist,
					   List *clauses,
					   List *custom_plans)
{
	CrossmatchJoinPath	   *gpath = (CrossmatchJoinPath *) best_path;
	List				   *joinrestrictclauses = gpath->joinrestrictinfo;
	List				   *joinclauses; /* NOTE: do we really need it? */
	List				   *otherclauses;
	CustomScan			   *cscan;

	if (IS_OUTER_JOIN(gpath->jointype))
	{
		extract_actual_join_clauses(joinrestrictclauses,
									&joinclauses, &otherclauses);
	}
	else
	{
		joinclauses = extract_actual_clauses(joinrestrictclauses,
											 false);
		otherclauses = NIL;
	}

	cscan = makeNode(CustomScan);
	cscan->scan.plan.targetlist = tlist;
	cscan->scan.plan.qual = NIL;
	cscan->scan.scanrelid = 0;

	cscan->custom_scan_tlist = tlist; /* TODO: recheck target list */

	elog(LOG, "tlist:");
	pprint(tlist);

	cscan->flags = best_path->flags;
	cscan->methods = &crossmatch_plan_methods;

	return &cscan->scan.plan;
}

static Node *
crossmatch_create_scan_state(CustomScan *node)
{
	CrossmatchScanState *scan_state = palloc0(sizeof(CrossmatchScanState));

	NodeSetTag(scan_state, T_CustomScanState);
	scan_state->css.flags = node->flags;
	scan_state->css.methods = &crossmatch_exec_methods;

	scan_state->css.ss.ps.ps_TupFromTlist = false;

	return (Node *) scan_state;
}

static void
crossmatch_begin(CustomScanState *node, EState *estate, int eflags)
{

}

static TupleTableSlot *
crossmatch_exec(CustomScanState *node)
{
	TupleTableSlot *slot = node->ss.ps.ps_ResultTupleSlot;
	TupleDesc	tupdesc = node->ss.ps.ps_ResultTupleSlot->tts_tupleDescriptor;
	ExprContext *econtext = node->ss.ps.ps_ProjInfo->pi_exprContext;
	HeapTuple	htup;

	HeapTupleData fetched_tup;

	ResetExprContext(econtext);

	/* TODO: fill with real data from joined tables */
	Datum values[4] = { DirectFunctionCall1(spherepoint_in, CStringGetDatum("(0d, 0d)")),
						DirectFunctionCall1(spherepoint_in, CStringGetDatum("(0d, 0d)")) };
	bool nulls[4] = {0,1,1,1};

	elog(LOG, "slot.natts: %d", tupdesc->natts);

	htup = heap_form_tuple(tupdesc, values, nulls);

	if (node->ss.ps.ps_ProjInfo->pi_itemIsDone != ExprEndResult)
	{
		TupleTableSlot *result;
		ExprDoneCond isDone;

		econtext->ecxt_scantuple = ExecStoreTuple(htup, slot, InvalidBuffer, false);

		result = ExecProject(node->ss.ps.ps_ProjInfo, &isDone);

		if (isDone != ExprEndResult)
		{
			node->ss.ps.ps_TupFromTlist = (isDone == ExprMultipleResult);
			return result;
		}
	}
	else
		ExecClearTuple(slot);

	return slot;
}

static void
crossmatch_end(CustomScanState *node)
{

}

static void
crossmatch_rescan(CustomScanState *node)
{

}

static void
crossmatch_explain(CustomScanState *node, List *ancestors, ExplainState *es)
{

}

void
_PG_init(void)
{
	elog(LOG, "loading pg_sphere");

	set_join_pathlist_next = set_join_pathlist_hook;
	set_join_pathlist_hook = join_pathlist_hook;

	crossmatch_path_methods.CustomName				= "CrossmatchJoin";
	crossmatch_path_methods.PlanCustomPath			= create_crossmatch_plan;

	crossmatch_plan_methods.CustomName 				= "CrossmatchJoin";
	crossmatch_plan_methods.CreateCustomScanState	= crossmatch_create_scan_state;

	crossmatch_exec_methods.CustomName				= "CrossmatchJoin";
	crossmatch_exec_methods.BeginCustomScan			= crossmatch_begin;
	crossmatch_exec_methods.ExecCustomScan			= crossmatch_exec;
	crossmatch_exec_methods.EndCustomScan			= crossmatch_end;
	crossmatch_exec_methods.ReScanCustomScan		= crossmatch_rescan;
	crossmatch_exec_methods.MarkPosCustomScan		= NULL;
	crossmatch_exec_methods.RestrPosCustomScan		= NULL;
	crossmatch_exec_methods.ExplainCustomScan		= crossmatch_explain;
}
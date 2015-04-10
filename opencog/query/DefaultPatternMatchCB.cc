/*
 * DefaultPatternMatchCB.cc
 *
 * Copyright (C) 2008,2009,2014 Linas Vepstas
 *
 * Author: Linas Vepstas <linasvepstas@gmail.com>  February 2008
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License v3 as
 * published by the Free Software Foundation and including the exceptions
 * at http://opencog.org/wiki/Licenses
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program; if not, write to:
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <opencog/atoms/execution/EvaluationLink.h>
#include <opencog/atomutils/FindUtils.h>
#include <opencog/atoms/bind/BetaRedex.h>

#include "DefaultPatternMatchCB.h"
#include "PatternMatchEngine.h"

using namespace opencog;

// Uncomment below to enable debug print
// #define DEBUG
#ifdef DEBUG
#define dbgprt(f, varargs...) printf(f, ##varargs)
#else
#define dbgprt(f, varargs...)
#endif

/* ======================================================== */

// Find a good place to start the search.
//
// The handle h points to a clause.  In principle, it is enough to
// simply find a constant in the clause, and just start there. In
// practice, this can be an awful way to do things. So, for example,
// most "typical" clauses will be of the form
//
//    EvaluationLink
//        PredicateNode "blah"
//        ListLink
//            VariableNode $var
//            ConceptNode  "item"
//
// Typically, the incoming set for "blah" will be huge, so starting the
// search there would be a poor choice. Typically, the incoming set to
// "item" will be much smaller, and so makes a better choice.  The code
// below tries to pass over "blah" and pick "item" instead.  It does so
// by comparing the size of the incoming sets of the two constants, and
// picking the one with the smaller ("thinner") incoming set. Note that
// this is a form of "greedy" search.
//
// Atoms that are inside of dynamically-evaluatable terms are not
// considered. That's because groundings for such terms might not exist
// in the atomspace, so a search that starts there is doomed to fail.
//
// Note that the algo explores the clause to its greatest depth. That's
// OK, because typical clauses are never very deep.
//
// A variant of this algo could incorporate the Attentional focus
// into the "thinnest" calculation, so that only high-AF atoms are
// considered.
//
// Note that the size of the incoming set really is a better measure,
// and not the depth.  So, for example, if "item" has a huge incoming
// set, but "blah" does not, then "blah" is a much better place to
// start.
//
// size_t& depth will be set to the depth of the thinnest constant found.
// Handle& start will be set to the link containing that constant.
// size_t& width will be set to the incoming-set size of the thinnest
//               constant found.
// The returned value will be the constant at which to start the search.
// If no constant is found, then the returned value is the undefnied
// handle.
//
Handle
DefaultPatternMatchCB::find_starter(const Handle& h, size_t& depth,
                                    Handle& start, size_t& width)
{
	// If its a node, then we are done. Don't modify either depth or
	// start.
	Type t = h->getType();
	if (classserver().isNode(t)) {
		if (t != VARIABLE_NODE) {
			width = h->getIncomingSetSize();
			return h;
		}
		return Handle::UNDEFINED;
	}

	// Ignore all OrLink's. Picking a starter inside one of these
	// will almost surely be disconnected from the rest of the graph.
	if (OR_LINK == t)
		return Handle::UNDEFINED;

	// Ignore all dynamically-evaluatable links up front.
	if (_dynamic and _dynamic->find(h) != _dynamic->end())
		return Handle::UNDEFINED;

	// Iterate over all the handles in the outgoing set.
	// Find the deepest one that contains a constant, and start
	// the search there.  If there are two at the same depth,
	// then start with the skinnier one.
	size_t deepest = depth;
	start = Handle::UNDEFINED;
	Handle hdeepest(Handle::UNDEFINED);
	size_t thinnest = SIZE_MAX;

	// If there is a ComposeLink, then search it's definition instead.
	// but do this only at depth zero, so as to get us started; otherwise
	// we risk infinite descent if the compose is recursive.
	LinkPtr ll(LinkCast(h));
	if (0 == depth and BETA_REDEX == ll->getType())
	{
		BetaRedexPtr cpl(BetaRedexCast(ll));
		ll = LinkCast(cpl->beta_reduce());
	}
	for (Handle hunt : ll->getOutgoingSet())
	{
		size_t brdepth = depth + 1;
		size_t brwid = SIZE_MAX;
		Handle sbr(h);

		// Blow past the QuoteLinks, since they just screw up the search start.
		if (QUOTE_LINK == hunt->getType())
			hunt = LinkCast(hunt)->getOutgoingAtom(0);

		Handle s(find_starter(hunt, brdepth, sbr, brwid));

		if (s != Handle::UNDEFINED
		    and (brwid < thinnest
		         or (brwid == thinnest and deepest < brdepth)))
		{
			deepest = brdepth;
			hdeepest = s;
			start = sbr;
			thinnest = brwid;
		}

	}
	depth = deepest;
	width = thinnest;
	return hdeepest;
}

// Look at all the clauses, to find the "thinnest" one.
Handle DefaultPatternMatchCB::find_thinnest(const std::vector<Handle>& clauses,
                                            Handle& starter_pred,
                                            size_t& bestclause)
{
	size_t thinnest = SIZE_MAX;
	size_t deepest = 0;
	bestclause = 0;
	Handle best_start(Handle::UNDEFINED);
	starter_pred = Handle::UNDEFINED;

	size_t nc = clauses.size();
	for (size_t i=0; i < nc; i++)
	{
		Handle h(clauses[i]);
		size_t depth = 0;
		size_t width = SIZE_MAX;
		Handle pred(Handle::UNDEFINED);
		Handle start(find_starter(h, depth, pred, width));
		if (start != Handle::UNDEFINED
		    and (width < thinnest
		         or (width == thinnest and depth > deepest)))
		{
			thinnest = width;
			deepest = depth;
			bestclause = i;
			best_start = start;
			starter_pred = pred;
		}
	}

    return best_start;
}

/**
 * Search for solutions/groundings over all of the AtomSpace, using
 * some "reasonable" assumptions for what might be searched for. Or,
 * to put it bluntly, this search method *might* miss some possible
 * solutions, for certain "unusual" search types. The trade-off is
 * that this search algo should really be quite fast for "normal"
 * search types.
 *
 * This search algo makes the following (important) assumptions:
 *
 * 1) If none of the clauses have any variables in them, (that is, if
 *    all of the clauses are "constant" clauses) then the search will
 *    begin by looping over all links in the atomspace that have the
 *    same link type as the first clause.  This will fail to examine
 *    all possible solutions if the link_match() callback is leniant,
 *    and accepts a broader range of types than just this one. This
 *    seems like a reasonable limitation: trying to search all-possible
 *    link-types would be a huge performance impact, especially if the
 *    link_match() callback was not interested in this broadened search.
 *
 *    At any rate, this limitation doesn't even apply, because the
 *    current PatternMatch::do_match() method completely removes
 *    constant clauses anyway.  (It needs to do this in order to
 *    simplify handling of connected graphs, so that virtual atoms are
 *    properly handled.  This is based on the assumption that support
 *    for virtual atoms is more important than support for unusual
 *    link_match() callbacks.
 *
 * 2) Search will begin at the first non-variable node in the "thinnest"
 *    clause.  The thinnest clause is chosen, so as to improve performance;
 *    but this has no effect on the thoroughness of the search.  The search
 *    will proceed by exploring the entire incoming-set for this node.
 *    The search will NOT examine other non-variable node types.
 *    If the node_match() callback is willing to accept a broader range
 *    of node matches, esp. for this initial node, then many possible
 *    solutions will be missed.  This seems like a reasonable limitation:
 *    if you really want a very lenient node_match(), then use variables.
 *    Or you can implement your own initiate_search() callback.
 *
 * 3) If the clauses consist entirely of variables, then the search
 *    will start by looking for all links that are of the same type as
 *    the type of the first clause.  This can fail to find all possible
 *    matches, if the link_match() callback is willing to accept a larger
 *    set of types.  This is a reasonable limitation: anything looser
 *    would very seriously degrade performance; if you really need a
 *    very lenient link_match(), then use variables. Or you can
 *    implement your own initiate_search() callback.
 *
 * The above describes the limits to the "typical" search that this
 * algo can do well. In particular, if the constraint of 2) can be met,
 * then the search can be quite rapid, since incoming sets are often
 * quite small; and assumption 2) limits the search to "nearby",
 * connected atoms.
 *
 * Note that the default implementation of node_match() and link_match()
 * in this class does satisfy both 2) and 3), so this algo will work
 * correctly if these two methods are not overloaded with more callbacks
 * that are lenient about matching types.
 *
 * If you overload node_match(), and do so in a way that breaks
 * assumption 2), then you will scratch your head, thinking
 * "why did my search fail to find this obvious solution?" The answer
 * will be for you to create a new search algo, in a new class, that
 * overloads this one, and does what you want it to.  This class should
 * probably *not* be modified, since it is quite efficient for the
 * "normal" case.
 */
void DefaultPatternMatchCB::initiate_search(PatternMatchEngine *pme,
                                            const std::set<Handle> &vars,
                                            const std::vector<Handle> &clauses)
{
	// In principle, we could start our search at some node, any node,
	// that is not a variable. In practice, the search begins by
	// iterating over the incoming set of the node, and so, if it is
	// large, a huge amount of effort might be wasted exploring
	// dead-ends.  Thus, it pays off to start the search on the
	// node with the smallest ("narrowest" or "thinnest") incoming set
	// possible.  Thus, we look at all the clauses, to find the
	// "thinnest" one.
	//
	// Note also: the user is allowed to specify patterns that have
	// no constants in them at all.  In this case, the search is
	// performed by looping over all links of the given types.

	size_t bestclause;
	Handle best_start = find_thinnest(clauses, _starter_pred, bestclause);

	if ((Handle::UNDEFINED != best_start)
	    // and (Handle::UNDEFINED != _starter_pred)
	    // and (not vars.empty()))
	    )
	{
		_root = clauses[bestclause];
		dbgprt("Search start node: %s\n", best_start->toShortString().c_str());
		dbgprt("Start pred is: %s\n", _starter_pred->toShortString().c_str());

		// This should be calling the over-loaded virtual method
		// get_incoming_set(), so that, e.g. it gets sorted by attentional
		// focus in the AttentionalFocusCB class...
		IncomingSet iset = get_incoming_set(best_start);
		size_t sz = iset.size();
		for (size_t i = 0; i < sz; i++) {
			Handle h(iset[i]);
			dbgprt("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n");
			dbgprt("Loop candidate (%lu/%lu): %s\n", i+1, sz,
			       h->toShortString().c_str());
			bool rc = pme->explore_neighborhood(_root, _starter_pred, h);
			if (rc) break;
		}

		// If we are here, we are done.
		return;
	}

	// If we are here, then we could not find a clause at which to start,
	// as, apparently, the clauses consist entirely of variables!! So,
	// basically, we must search the entire!! atomspace, in this case.
	// Yes, this hurts.
	full_search(pme, clauses);
}

/// The defualt search tries to optimize by making some reasonable
/// assumptions about what is being looked for. But not every problem
/// fits those assumptions, so this method provides an exhaustive
/// search. Note that exhaustive searches can be exhaustingly long,
/// so watch out!
void DefaultPatternMatchCB::full_search(PatternMatchEngine *pme,
                                        const std::vector<Handle> &clauses)
{
	_root = clauses[0];
	_starter_pred = _root;

	dbgprt("Start pred is: %s\n", _starter_pred->toShortString().c_str());

	// Get type of the first item in the predicate list.
	Type ptype = _root->getType();

	// Plunge into the deep end - start looking at all viable
	// candidates in the AtomSpace.

	// XXX TODO -- as a performance optimization, we should try all
	// the different clauses, and find the one with the smallest number
	// of atoms of that type, or otherwise try to find a small ("thin")
	// incoming set to search over.
	//
	// If ptype is a VariableNode, then basically, the pattern says
	// "Search all of the atomspace." Literally. So this will blow up
	// if the atomspace is large.
	std::list<Handle> handle_set;
	if (VARIABLE_NODE != ptype)
		_as->getHandlesByType(back_inserter(handle_set), ptype);
	else
		_as->getHandlesByType(back_inserter(handle_set), ATOM, true);

#ifdef DEBUG
	size_t i = 0;
#endif
	for (const Handle& h : handle_set)
	{
		dbgprt("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n");
		dbgprt("Loop candidate (%lu/%lu): %s\n", ++i, handle_set.size(),
		       h->toShortString().c_str());
		bool rc = pme->explore_neighborhood(_root, _starter_pred, h);
		if (rc) break;
	}
}

/* ======================================================== */

bool DefaultPatternMatchCB::virtual_link_match(const Handle& virt, const Handle& gargs)
{
	// At this time, we expect all virutal links to be in one of two
	// forms: either EvaluationLink's or GreaterThanLink's.  The
	// EvaluationLinks should have the structure
	//
	//   EvaluationLink
	//       GroundedPredicateNode "scm:blah"
	//       ListLink
	//           Arg1Atom
	//           Arg2Atom
	//
	// The GreaterThanLink's should have the "obvious" structure
	//
	//   GreaterThanLink
	//       Arg1Atom
	//       Arg2Atom
	//
	// XXX TODO as discussed on the mailing list, we should perhaps first
	// see if the following can be found in the atomspace:
	//
	//   EvaluationLink
	//       PredicateNode "blah"  ; not Grounded any more, and scm: stripped
	//       ListLink
	//           Arg1Atom
	//           Arg2Atom
	//
	// If it does, we should declare a match.  If not, only then run the
	// do_evaluate callback.  Alternately, perhaps the 
	// EvaluationLink::do_evaluate() method should do this ??? Its a toss-up.

	TruthValuePtr tvp(EvaluationLink::do_evaluate(_as, gargs));

	// XXX FIXME: we are making a crsip-logic go/no-go decision
	// based on the TV strength. Perhaps something more subtle might be
	// wanted, here.
	bool relation_holds = tvp->getMean() > 0.5;
	return relation_holds;
}

/* ===================== END OF FILE ===================== */

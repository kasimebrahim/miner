/*
 * AttentionalFocusCB.h
 *
 * Copyright (C) 2014 Misgana Bayetta
 *
 * Author: Misgana Bayetta <misgana.bayetta@gmail.com>  July 2014
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
#ifndef _ATTENTIONAL_FOCUS_CB_H
#define _ATTENTIONAL_FOCUS_CB_H

#include "DefaultPatternMatchCB.h"

namespace opencog {

class AttentionalFocusCB: public virtual DefaultPatternMatchCB
{
private:
	static bool compare_sti(LinkPtr lptr1, LinkPtr lptr2)
	{
		return lptr1->getSTI() > lptr2->getSTI();
	}
public:
	AttentionalFocusCB(AtomSpace * as) :
		DefaultPatternMatchCB(as) {}

	// Only match nodes if they are in the attentional focus
	bool node_match(const Handle&, const Handle&);

	// Only match links if they are in the attentional focus
	bool link_match(const LinkPtr&, const LinkPtr&);

	// Only get incomming sets that are in the attentional focus
	IncomingSet get_incoming_set(const Handle&);

	// Starts from atoms in the attentional focus, with the right types
	void perform_search(PatternMatchEngine *pme,
	                    const std::set<Handle> &vars,
	                    const std::vector<Handle> &clauses,
	                    const std::vector<Handle> &negations);
};

} //namespace opencog
#endif /* _ATTENTIONALFOCUSCB_H */

/*
 * Copyright (C) 2004 Apple Computer, Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */
  
#include "dom_selection.h"

#include "dom_caretposition.h"
#include "dom_docimpl.h"
#include "dom_elementimpl.h"
#include "dom_node.h"
#include "dom_nodeimpl.h"
#include "dom_positioniterator.h"
#include "dom_string.h"
#include "dom_textimpl.h"
#include "dom2_range.h"
#include "htmltags.h"
#include "khtml_part.h"
#include "khtmlview.h"
#include "qevent.h"
#include "qpainter.h"
#include "qrect.h"
#include "render_object.h"
#include "render_style.h"
#include "render_text.h"

#if APPLE_CHANGES
#include "KWQAssertions.h"
#else
#define ASSERT(assertion) assert(assertion)
#endif

#define EDIT_DEBUG 0

using khtml::findWordBoundary;
using khtml::InlineTextBox;
using khtml::RenderObject;
using khtml::RenderText;

namespace DOM {

static Selection selectionForLine(const Position &position);

static inline Position &emptyPosition()
{
    static Position EmptyPosition = Position();
    return EmptyPosition;
}

Selection::Selection()
{
    init();
}

Selection::Selection(const Position &pos)
{
    init();
    assignBaseAndExtent(pos, pos);
    validate();
}

Selection::Selection(const Range &r)
{
    const Position start(r.startContainer().handle(), r.startOffset());
    const Position end(r.endContainer().handle(), r.endOffset());

    init();
    assignBaseAndExtent(start, end);
    validate();
}

Selection::Selection(const Position &base, const Position &extent)
{
    init();
    assignBaseAndExtent(base, extent);
    validate();
}

Selection::Selection(const Selection &o)
{
    init();
    
    assignBaseAndExtent(o.base(), o.extent());
    assignStartAndEnd(o.start(), o.end());

    m_state = o.m_state;
    m_affinity = o.m_affinity;

    m_baseIsStart = o.m_baseIsStart;
    m_needsCaretLayout = o.m_needsCaretLayout;
    m_modifyBiasSet = o.m_modifyBiasSet;
    
    // Only copy the coordinates over if the other object
    // has had a layout, otherwise keep the current
    // coordinates. This prevents drawing artifacts from
    // remaining when the caret is painted and then moves,
    // and the old rectangle needs to be repainted.
    if (!m_needsCaretLayout) {
        m_caretRect = o.m_caretRect;
    }
}

void Selection::init()
{
    m_base = m_extent = m_start = m_end = emptyPosition();
    m_state = NONE; 
    m_baseIsStart = true;
    m_needsCaretLayout = true;
    m_modifyBiasSet = false;
    m_affinity = DOWNSTREAM;
}

Selection &Selection::operator=(const Selection &o)
{
    assignBaseAndExtent(o.base(), o.extent());
    assignStartAndEnd(o.start(), o.end());

    m_state = o.m_state;
    m_affinity = o.m_affinity;

    m_baseIsStart = o.m_baseIsStart;
    m_needsCaretLayout = o.m_needsCaretLayout;
    m_modifyBiasSet = o.m_modifyBiasSet;
    
    // Only copy the coordinates over if the other object
    // has had a layout, otherwise keep the current
    // coordinates. This prevents drawing artifacts from
    // remaining when the caret is painted and then moves,
    // and the old rectangle needs to be repainted.
    if (!m_needsCaretLayout) {
        m_caretRect = o.m_caretRect;
    }
    
    return *this;
}

void Selection::setAffinity(EAffinity affinity)
{
    if (affinity == m_affinity)
        return;
        
    m_affinity = affinity;
    setNeedsLayout();
}

void Selection::moveTo(const Range &r)
{
    Position start(r.startContainer().handle(), r.startOffset());
    Position end(r.endContainer().handle(), r.endOffset());
    moveTo(start, end);
}

void Selection::moveTo(const Selection &o)
{
    moveTo(o.start(), o.end());
}

void Selection::moveTo(const Position &pos)
{
    moveTo(pos, pos);
}

void Selection::moveTo(const Position &base, const Position &extent)
{
    assignBaseAndExtent(base, extent);
    validate();
}

CaretPosition Selection::modifyExtendingRightForward(ETextGranularity granularity)
{
    if (!m_modifyBiasSet) {
        m_modifyBiasSet = true;
        assignBaseAndExtent(start(), end());
    }
    CaretPosition pos = extent();
    switch (granularity) {
        case CHARACTER:
            pos = pos.next();
            break;
        case WORD:
            pos = pos.deepEquivalent().nextWordPosition();
            break;
        case PARAGRAPH:
            // "Next paragraph" not implemented yet. Fall through to LINE.
        case LINE:
            pos = pos.deepEquivalent().nextLinePosition(xPosForVerticalArrowNavigation(EXTENT));
            break;
        case DOCUMENT: {
            ElementImpl *de = start().node()->getDocument()->documentElement();
            pos = CaretPosition(de, de ? de->childNodeCount() : 0);
            break;
        }
        case LINE_BOUNDARY:
            pos = selectionForLine(end()).end();
            break;
        case PARAGRAPH_BOUNDARY:
            pos = endParagraphBoundary(end());
            break;
    }
    return pos;
}

CaretPosition Selection::modifyMovingRightForward(ETextGranularity granularity)
{
    m_modifyBiasSet = false;
    CaretPosition pos;
    switch (granularity) {
        case CHARACTER:
            if (state() == RANGE) 
                pos = end();
            else
                pos = CaretPosition(extent()).next();
            break;
        case WORD:
            pos = extent().nextWordPosition();
            break;
        case PARAGRAPH:
            // "Next paragraph" not implemented yet. Fall through to LINE.
        case LINE:
            pos = end().nextLinePosition(xPosForVerticalArrowNavigation(END, state() == RANGE));
            break;
        case DOCUMENT: {
            ElementImpl *de = start().node()->getDocument()->documentElement();
            pos = CaretPosition(de, de ? de->childNodeCount() : 0);
            break;
        }
        case LINE_BOUNDARY:
            pos = selectionForLine(end()).end();
            break;
        case PARAGRAPH_BOUNDARY:
            pos = endParagraphBoundary(end());
            break;
    }
    return pos;
}

CaretPosition Selection::modifyExtendingLeftBackward(ETextGranularity granularity)
{
    if (!m_modifyBiasSet) {
        m_modifyBiasSet = true;
        assignBaseAndExtent(end(), start());
    }
    CaretPosition pos = extent();
    switch (granularity) {
        case CHARACTER:
            pos = pos.previous();
            break;
        case WORD:
            pos = pos.deepEquivalent().previousWordPosition();
            break;
        case PARAGRAPH:
            // "Previous paragraph" not implemented yet. Fall through to LINE.
        case LINE:
            pos = pos.deepEquivalent().previousLinePosition(xPosForVerticalArrowNavigation(EXTENT));
            break;
        case DOCUMENT:
            pos = CaretPosition(start().node()->getDocument()->documentElement(), 0);
            break;
        case LINE_BOUNDARY:
            pos = selectionForLine(start()).start();
            break;
        case PARAGRAPH_BOUNDARY:
            pos = startParagraphBoundary(start());
            break;
    }
    return pos;
}

CaretPosition Selection::modifyMovingLeftBackward(ETextGranularity granularity)
{
    m_modifyBiasSet = false;
    CaretPosition pos;
    switch (granularity) {
        case CHARACTER:
            if (state() == RANGE) 
                pos = start();
            else
                pos = CaretPosition(extent()).previous();
            break;
        case WORD:
            pos = extent().previousWordPosition();
            break;
        case PARAGRAPH:
            // "Previous paragraph" not implemented yet. Fall through to LINE.
        case LINE:
            pos = start().previousLinePosition(xPosForVerticalArrowNavigation(START, state() == RANGE));
            break;
        case DOCUMENT:
            pos = CaretPosition(start().node()->getDocument()->documentElement(), 0);
            break;
        case LINE_BOUNDARY:
            pos = selectionForLine(start()).start();
            break;
        case PARAGRAPH_BOUNDARY:
            pos = startParagraphBoundary(start()).deepEquivalent();
            break;
    }
    return pos;
}

bool Selection::modify(EAlter alter, EDirection dir, ETextGranularity granularity)
{
    CaretPosition pos;

    switch (dir) {
        // EDIT FIXME: These need to handle bidi
        case RIGHT:
        case FORWARD:
            if (alter == EXTEND)
                pos = modifyExtendingRightForward(granularity);
            else
                pos = modifyMovingRightForward(granularity);
            break;
        case LEFT:
        case BACKWARD:
            if (alter == EXTEND)
                pos = modifyExtendingLeftBackward(granularity);
            else
                pos = modifyMovingLeftBackward(granularity);
            break;
    }
    
    if (pos.isEmpty())
        return false;
    
    if (alter == MOVE)
        moveTo(pos.deepEquivalent());
    else // alter == EXTEND
        setExtent(pos.deepEquivalent());
    
    return true;
}

bool Selection::expandUsingGranularity(ETextGranularity granularity)
{
    if (state() == NONE)
        return false;
        
    validate(granularity);
    return true;
}

int Selection::xPosForVerticalArrowNavigation(EPositionType type, bool recalc) const
{
    int x = 0;

    if (state() == NONE)
        return x;

    Position pos;
    switch (type) {
        case START:
            pos = start();
            break;
        case END:
            pos = end();
            break;
        case BASE:
            pos = base();
            break;
        case EXTENT:
            pos = extent();
            break;
    }

    KHTMLPart *part = pos.node()->getDocument()->part();
    if (!part)
        return x;
        
    if (recalc || part->xPosForVerticalArrowNavigation() == KHTMLPart::NoXPosForVerticalArrowNavigation) {
        x = pos.node()->renderer()->caretRect(pos.offset(), false).x();
        part->setXPosForVerticalArrowNavigation(x);
    }
    else {
        x = part->xPosForVerticalArrowNavigation();
    }

    return x;
}

void Selection::clear()
{
    assignBaseAndExtent(emptyPosition(), emptyPosition());
    validate();
}

void Selection::setBase(const Position &pos)
{
    assignBase(pos);
    validate();
}

void Selection::setExtent(const Position &pos)
{
    assignExtent(pos);
    validate();
}

void Selection::setBaseAndExtent(const Position &base, const Position &extent)
{
    assignBaseAndExtent(base, extent);
    validate();
}

void Selection::setStart(const Position &pos)
{
    assignStart(pos);
    validate();
}

void Selection::setEnd(const Position &pos)
{
    assignEnd(pos);
    validate();
}

void Selection::setStartAndEnd(const Position &start, const Position &end)
{
    assignStartAndEnd(start, end);
    validate();
}

void Selection::setNeedsLayout(bool flag)
{
    m_needsCaretLayout = flag;
}

Range Selection::toRange() const
{
    if (isEmpty())
        return Range();

    // Make sure we have an updated layout since this function is called
    // in the course of running edit commands which modify the DOM.
    // Failing to call this can result in equivalentXXXPosition calls returning
    // incorrect results.
    start().node()->getDocument()->updateLayout();

    Position s, e;
    if (state() == CARET) {
        // If the selection is a caret, move the range start upstream. This helps us match
        // the conventions of text editors tested, which make style determinations based
        // on the character before the caret, if any. 
        s = start().upstream().equivalentRangeCompliantPosition();
        e = s;
    }
    else {
        // If the selection is a range, select the minimum range that encompasses the selection.
        // Again, this is to match the conventions of text editors tested, which make style 
        // determinations based on the first character of the selection. 
        // For instance, this operation helps to make sure that the "X" selected below is the 
        // only thing selected. The range should not be allowed to "leak" out to the end of the 
        // previous text node, or to the beginning of the next text node, each of which has a 
        // different style.
        // 
        // On a treasure map, <b>X</b> marks the spot.
        //                       ^ selected
        //
        ASSERT(state() == RANGE);
        s = start().downstream();
        e = end().upstream();
        if ((s.node() == e.node() && s.offset() > e.offset()) || !nodeIsBeforeNode(s.node(), e.node())) {
            // Make sure the start is before the end.
            // The end can wind up before the start if collapsed whitespace is the only thing selected.
            Position tmp = s;
            s = e;
            e = tmp;
        }
        s = s.equivalentRangeCompliantPosition();
        e = e.equivalentRangeCompliantPosition();
    }

    return Range(s.node(), s.offset(), e.node(), e.offset());
}

void Selection::layoutCaret()
{
    if (state() != CARET || isEmpty() || !start().node()->inDocument()) {
        m_caretRect = QRect();
        return;
    }
    
    // EDIT FIXME: Enhance call to pass along selection 
    // upstream/downstream affinity to get the right position.
    m_caretRect = start().node()->renderer()->caretRect(start().offset(), false);

    m_needsCaretLayout = false;
}

QRect Selection::getRepaintRect() const
{
    if (m_needsCaretLayout) {
        const_cast<Selection *>(this)->layoutCaret();
    }

    // EDIT FIXME: fudge one pixel on each side to make sure we don't leave behind artifacts
    if (m_caretRect.isEmpty())
        return QRect();
    return QRect(m_caretRect.left() - 1, m_caretRect.top() - 1,
        m_caretRect.width() + 2, m_caretRect.height() + 2);
}

void Selection::needsCaretRepaint()
{
    if (isEmpty())
        return;

    if (!start().node()->getDocument())
        return;

    KHTMLView *v = start().node()->getDocument()->view();
    if (!v)
        return;

    if (m_needsCaretLayout) {
        // repaint old position and calculate new position
        v->updateContents(getRepaintRect(), false);
        layoutCaret();
        
        // EDIT FIXME: This is an unfortunate hack.
        // Basically, we can't trust this layout position since we 
        // can't guarantee that the check to see if we are in unrendered 
        // content will work at this point. We may have to wait for
        // a layout and re-render of the document to happen. So, resetting this
        // flag will cause another caret layout to happen the first time
        // that we try to paint the caret after this call. That one will work since
        // it happens after the document has accounted for any editing
        // changes which may have been done.
        // And, we need to leave this layout here so the caret moves right 
        // away after clicking.
        m_needsCaretLayout = true;
    }
    v->updateContents(getRepaintRect(), false);
}

void Selection::paintCaret(QPainter *p, const QRect &rect)
{
    if (m_state != CARET)
        return;

    if (m_needsCaretLayout)
        layoutCaret();

    if (m_caretRect.isValid())
        p->fillRect(m_caretRect & rect, QBrush());
}

void Selection::validate(ETextGranularity granularity)
{
    // Move the selection to rendered positions, if possible.
    Position originalBase(base());
    bool baseAndExtentEqual = base() == extent();
    bool updatedLayout = false;
    if (base().notEmpty()) {
        base().node()->getDocument()->updateLayout();
        updatedLayout = true;
        assignBase(base().equivalentDeepPosition().closestRenderedPosition(affinity()));
        if (baseAndExtentEqual)
            assignExtent(base());
    }
    if (extent().notEmpty() && !baseAndExtentEqual) {
        if (!updatedLayout)
            extent().node()->getDocument()->updateLayout();
        assignExtent(extent().equivalentDeepPosition().closestRenderedPosition(affinity()));
    }

    // Make sure we do not have a dangling start or end
    if (base().isEmpty() && extent().isEmpty()) {
        // Move the position to the enclosingBlockFlowElement of the original base, if possible.
        // This has the effect of flashing the caret somewhere when a rendered position for
        // the base and extent cannot be found.
        if (originalBase.notEmpty()) {
            Position pos(originalBase.node()->enclosingBlockFlowElement(), 0);
            assignBaseAndExtent(pos, pos);
            assignStartAndEnd(pos, pos);
        }
        else {
            // We have no position to work with. See if the BODY element of the page
            // is contentEditable. If it is, put the caret there.
            //NodeImpl *node = document()
            assignStartAndEnd(emptyPosition(), emptyPosition());
        }
        m_baseIsStart = true;
    }
    else if (base().isEmpty()) {
        assignBase(extent());
        m_baseIsStart = true;
    }
    else if (extent().isEmpty()) {
        assignExtent(base());
        m_baseIsStart = true;
    }
    else {
        if (base().node() == extent().node()) {
            if (base().offset() > extent().offset())
                m_baseIsStart = false;
            else 
                m_baseIsStart = true;
        }
        else if (nodeIsBeforeNode(base().node(), extent().node()))
            m_baseIsStart = true;
        else
            m_baseIsStart = false;
    }

    // calculate the correct start and end positions
    switch (granularity) {
        case CHARACTER:
            if (m_baseIsStart)
                assignStartAndEnd(base(), extent());
            else
                assignStartAndEnd(extent(), base());
            break;
        case WORD: {
            // FIXME: This doesn't handle words that cross node boundaries.
            int baseStartOffset = base().offset();
            int baseEndOffset = base().offset();
            int extentStartOffset = extent().offset();
            int extentEndOffset = extent().offset();
            if (base().notEmpty() && (base().node()->nodeType() == Node::TEXT_NODE || base().node()->nodeType() == Node::CDATA_SECTION_NODE)) {
                DOMString t = base().node()->nodeValue();
                QChar *chars = t.unicode();
                uint len = t.length();
                findWordBoundary(chars, len, base().offset(), &baseStartOffset, &baseEndOffset);
            }
            if (extent().notEmpty() && (extent().node()->nodeType() == Node::TEXT_NODE || extent().node()->nodeType() == Node::CDATA_SECTION_NODE)) {
                DOMString t = extent().node()->nodeValue();
                QChar *chars = t.unicode();
                uint len = t.length();
                findWordBoundary(chars, len, extent().offset(), &extentStartOffset, &extentEndOffset);
            }
            if (m_baseIsStart) {
                assignStart(CaretPosition(base().node(), baseStartOffset).deepEquivalent());
                assignEnd(CaretPosition(extent().node(), extentEndOffset).deepEquivalent());
            }
            else {
                assignStart(CaretPosition(extent().node(), extentStartOffset).deepEquivalent());
                assignEnd(CaretPosition(base().node(), baseEndOffset).deepEquivalent());
            }
            break;
        }
        case LINE:
        case LINE_BOUNDARY: {
            Selection baseSelection = *this;
            Selection extentSelection = *this;
            Selection baseLine = selectionForLine(base());
            if (baseLine.notEmpty()) {
                baseSelection = baseLine;
            }
            Selection extentLine = selectionForLine(extent());
            if (extentLine.notEmpty()) {
                extentSelection = extentLine;
            }
            if (m_baseIsStart) {
                assignStart(baseSelection.start());
                assignEnd(extentSelection.end());
            } else {
                assignStart(extentSelection.start());
                assignEnd(baseSelection.end());
            }
            break;
        }
        case PARAGRAPH:
            if (m_baseIsStart) {
                assignStart(startParagraphBoundary(base()).deepEquivalent());
                assignEnd(endParagraphBoundary(extent(), IncludeLineBreak).deepEquivalent());
            } else {
                assignStart(startParagraphBoundary(extent()).deepEquivalent());
                assignEnd(endParagraphBoundary(base(), IncludeLineBreak).deepEquivalent());
            }
            break;
        case DOCUMENT: {
            NodeImpl *de = start().node()->getDocument()->documentElement();
            assignStart(CaretPosition(de, 0).deepEquivalent());
            assignEnd(CaretPosition(de, de ? de->childNodeCount() : 0).deepEquivalent());
            break;
        }
        case PARAGRAPH_BOUNDARY:
            if (m_baseIsStart) {
                assignStart(startParagraphBoundary(base()).deepEquivalent());
                assignEnd(endParagraphBoundary(extent()).deepEquivalent());
            } else {
                assignStart(startParagraphBoundary(extent()).deepEquivalent());
                assignEnd(endParagraphBoundary(base()).deepEquivalent());
            }
            break;
    }

    // adjust the state
    if (start().isEmpty() && end().isEmpty()) {
        m_state = NONE;
    }
    else if (start() == end() || start().upstream(StayInBlock) == end().upstream(StayInBlock)) {
        m_state = CARET;
    }
    else {
        m_state = RANGE;
        // "Constrain" the selection to be the smallest equivalent range of nodes.
        // This is a somewhat arbitrary choice, but experience shows that it is
        // useful to make to make the selection "canonical" (if only for
        // purposes of comparing selections). This is an ideal point of the code
        // to do this operation, since all selection changes that result in a RANGE 
        // come through here before anyone uses it.
        assignStart(start().downstream(StayInBlock));
        assignEnd(end().upstream(StayInBlock));
    }

    m_needsCaretLayout = true;
    
#if EDIT_DEBUG
    debugPosition();
#endif
}

bool Selection::moveToRenderedContent()
{
    if (isEmpty())
        return false;
        
    if (m_state != CARET)
        return false;

    Position pos = start();
    if (pos.inRenderedContent())
        return true;
        
    // not currently rendered, try moving to prev
    Position prev = pos.previousCharacterPosition();
    if (prev != pos && prev.node()->inSameContainingBlockFlowElement(pos.node())) {
        moveTo(prev);
        return true;
    }

    // could not be moved to prev, try next
    Position next = pos.nextCharacterPosition();
    if (next != pos && next.node()->inSameContainingBlockFlowElement(pos.node())) {
        moveTo(next);
        return true;
    }
    
    return false;
}

bool Selection::nodeIsBeforeNode(NodeImpl *n1, NodeImpl *n2)
{
    if (!n1 || !n2) 
        return true;

    if (n1 == n2)
        return true;

    bool result = false;
    int n1Depth = 0;
    int n2Depth = 0;

    // First we find the depths of the two nodes in the tree (n1Depth, n2Depth)
    NodeImpl *n = n1;
    while (n->parentNode()) {
        n = n->parentNode();
        n1Depth++;
    }
    n = n2;
    while (n->parentNode()) {
        n = n->parentNode();
        n2Depth++;
    }
    // Climb up the tree with the deeper node, until both nodes have equal depth
    while (n2Depth > n1Depth) {
        n2 = n2->parentNode();
        n2Depth--;
    }
    while (n1Depth > n2Depth) {
        n1 = n1->parentNode();
        n1Depth--;
    }
    // Climb the tree with both n1 and n2 until they have the same parent
    while (n1->parentNode() != n2->parentNode()) {
        n1 = n1->parentNode();
        n2 = n2->parentNode();
    }
    // Iterate through the parent's children until n1 or n2 is found
    n = n1->parentNode() ? n1->parentNode()->firstChild() : n1->firstChild();
    while (n) {
        if (n == n1) {
            result = true;
            break;
        }
        else if (n == n2) {
            result = false;
            break;
        }
        n = n->nextSibling();
    }
    return result;
}

static Position startOfFirstRunAt(RenderObject *renderNode, int y)
{
    for (RenderObject *n = renderNode; n; n = n->nextSibling()) {
        if (n->isText()) {
            RenderText *textRenderer = static_cast<RenderText *>(n);
            for (InlineTextBox* box = textRenderer->firstTextBox(); box; box = box->nextTextBox())
                if (box->m_y == y)
                    return Position(textRenderer->element(), box->m_start);
        }
        
        Position position = startOfFirstRunAt(n->firstChild(), y);
        if (position.notEmpty())
            return position;
    }
    
    return Position();
}

static Position endOfLastRunAt(RenderObject *renderNode, int y)
{
    RenderObject *n = renderNode;
    if (!n)
        return Position();
    if (RenderObject *parent = n->parent())
        n = parent->lastChild();
    
    while (1) {
        Position position = endOfLastRunAt(n->firstChild(), y);
        if (position.notEmpty())
            return position;
        
        if (n->isText()) {
            RenderText *textRenderer = static_cast<RenderText *>(n);
            for (InlineTextBox* box = textRenderer->lastTextBox(); box; box = box->prevTextBox())
                if (box->m_y == y)
                    return Position(textRenderer->element(), box->m_start + box->m_len);
        }
        
        if (n == renderNode)
            return Position();
        
        n = n->previousSibling();
    }
}

static Selection selectionForLine(const Position &position)
{
    NodeImpl *node = position.node();

    if (!node)
        return Selection();

    switch (node->nodeType()) {
        case Node::TEXT_NODE:
        case Node::CDATA_SECTION_NODE:
            break;
        default:
            return Selection();
    }

    RenderText *renderer = static_cast<RenderText *>(node->renderer());

    int pos;
    InlineTextBox *run = renderer->findNextInlineTextBox(position.offset(), pos);
    if (!run)
        return Selection();
        
    int selectionPointY = run->m_y;
    
    // Go up to first non-inline element.
    RenderObject *renderNode = renderer;
    while (renderNode && renderNode->isInline())
        renderNode = renderNode->parent();
    renderNode = renderNode->firstChild();
    
    // Look for all the first child in the block that is on the same line
    // as the selection point.
    Position start = startOfFirstRunAt(renderNode, selectionPointY);
    if (start.isEmpty())
        return Selection();

    // Look for all the last child in the block that is on the same line
    // as the selection point.
    Position end = endOfLastRunAt(renderNode, selectionPointY);
    if (end.isEmpty())
        return Selection();
    
    return Selection(start, end);
}

void Selection::debugRenderer(RenderObject *r, bool selected) const
{
    if (r->node()->isElementNode()) {
        ElementImpl *element = static_cast<ElementImpl *>(r->node());
        fprintf(stderr, "%s%s\n", selected ? "==> " : "    ", element->tagName().string().latin1());
    }
    else if (r->isText()) {
        RenderText *textRenderer = static_cast<RenderText *>(r);
        if (textRenderer->stringLength() == 0 || !textRenderer->firstTextBox()) {
            fprintf(stderr, "%s#text (empty)\n", selected ? "==> " : "    ");
            return;
        }
        
        static const int max = 36;
        QString text = DOMString(textRenderer->string()).string();
        int textLength = text.length();
        if (selected) {
            int offset = 0;
            if (r->node() == start().node())
                offset = start().offset();
            else if (r->node() == end().node())
                offset = end().offset();
                
            int pos;
            InlineTextBox *box = textRenderer->findNextInlineTextBox(offset, pos);
            text = text.mid(box->m_start, box->m_len);
            
            QString show;
            int mid = max / 2;
            int caret = 0;
            
            // text is shorter than max
            if (textLength < max) {
                show = text;
                caret = pos;
            }
            
            // too few characters to left
            else if (pos - mid < 0) {
                show = text.left(max - 3) + "...";
                caret = pos;
            }
            
            // enough characters on each side
            else if (pos - mid >= 0 && pos + mid <= textLength) {
                show = "..." + text.mid(pos - mid + 3, max - 6) + "...";
                caret = mid;
            }
            
            // too few characters on right
            else {
                show = "..." + text.right(max - 3);
                caret = pos - (textLength - show.length());
            }
            
            show = show.replace("\n", " ");
            show = show.replace("\r", " ");
            fprintf(stderr, "==> #text : \"%s\" at offset %d\n", show.latin1(), pos);
            fprintf(stderr, "           ");
            for (int i = 0; i < caret; i++)
                fprintf(stderr, " ");
            fprintf(stderr, "^\n");
        }
        else {
            if ((int)text.length() > max)
                text = text.left(max - 3) + "...";
            else
                text = text.left(max);
            fprintf(stderr, "    #text : \"%s\"\n", text.latin1());
        }
    }
}

void Selection::debugPosition() const
{
    if (!start().node())
        return;

    //static int context = 5;
    
    //RenderObject *r = 0;

    fprintf(stderr, "Selection =================\n");

    if (start() == end()) {
        Position pos = start();
        Position upstream = pos.upstream();
        Position downstream = pos.downstream();
        fprintf(stderr, "upstream:   %s %p:%d\n", getTagName(upstream.node()->id()).string().latin1(), upstream.node(), upstream.offset());
        fprintf(stderr, "pos:        %s %p:%d\n", getTagName(pos.node()->id()).string().latin1(), pos.node(), pos.offset());
        fprintf(stderr, "downstream: %s %p:%d\n", getTagName(downstream.node()->id()).string().latin1(), downstream.node(), downstream.offset());
    }
    else {
        Position pos = start();
        Position upstream = pos.upstream();
        Position downstream = pos.downstream();
        fprintf(stderr, "upstream:   %s %p:%d\n", getTagName(upstream.node()->id()).string().latin1(), upstream.node(), upstream.offset());
        fprintf(stderr, "start:      %s %p:%d\n", getTagName(pos.node()->id()).string().latin1(), pos.node(), pos.offset());
        fprintf(stderr, "downstream: %s %p:%d\n", getTagName(downstream.node()->id()).string().latin1(), downstream.node(), downstream.offset());
        fprintf(stderr, "-----------------------------------\n");
        pos = end();
        upstream = pos.upstream();
        downstream = pos.downstream();
        fprintf(stderr, "upstream:   %s %p:%d\n", getTagName(upstream.node()->id()).string().latin1(), upstream.node(), upstream.offset());
        fprintf(stderr, "end:        %s %p:%d\n", getTagName(pos.node()->id()).string().latin1(), pos.node(), pos.offset());
        fprintf(stderr, "downstream: %s %p:%d\n", getTagName(downstream.node()->id()).string().latin1(), downstream.node(), downstream.offset());
        fprintf(stderr, "-----------------------------------\n");
    }
          
#if 0
    int back = 0;
    r = start().node()->renderer();
    for (int i = 0; i < context; i++, back++) {
        if (r->previousRenderer())
            r = r->previousRenderer();
        else
            break;
    }
    for (int i = 0; i < back; i++) {
        debugRenderer(r, false);
        r = r->nextRenderer();
    }


    fprintf(stderr, "\n");

    if (start().node() == end().node())
        debugRenderer(start().node()->renderer(), true);
    else
        for (r = start().node()->renderer(); r && r != end().node()->renderer(); r = r->nextRenderer())
            debugRenderer(r, true);
    
    fprintf(stderr, "\n");
    
    r = end().node()->renderer();
    for (int i = 0; i < context; i++) {
        if (r->nextRenderer()) {
            r = r->nextRenderer();
            debugRenderer(r, false);
        }
        else
            break;
    }
#endif

    fprintf(stderr, "================================\n");
}

#ifndef NDEBUG
#define FormatBufferSize 1024
void Selection::formatForDebugger(char *buffer, unsigned length) const
{
    DOMString result;
    DOMString s;
    
    if (isEmpty()) {
        result = "<empty>";
    }
    else {
        char s[FormatBufferSize];
        result += "from ";
        m_start.formatForDebugger(s, FormatBufferSize);
        result += s;
        result += " to ";
        m_end.formatForDebugger(s, FormatBufferSize);
        result += s;
    }
          
    strncpy(buffer, result.string().latin1(), length - 1);
}
#undef FormatBufferSize
#endif

} // namespace DOM

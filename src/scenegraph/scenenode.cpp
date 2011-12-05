/***************************************************************************
**
** Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (directui@nokia.com)
**
** This file is part of mcompositor.
**
** If you have questions regarding the use of this file, please contact
** Nokia at directui@nokia.com.
**
** This library is free software; you can redistribute it and/or
** modify it under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation
** and appearing in the file LICENSE.LGPL included in the packaging
** of this file.
**
****************************************************************************/

#include "scenenode.h"
#include "scenerender.h"

#define INIT_POSITION 0x24CDEB

/*!
  \class SceneNode
  \brief SceneNode is an asbstract base class for nodes in the scenegraph.

  SceneNode represent nodes in the graph that are traversed by the renderer
  to describe the scene. The decision of traversal in each node is determined
  by re-implementing the virtual processNode() function;

  A node can have many children but can only have one parent - with the 
  exception of an EffectNode which is a container for shader effects and can
  be shared accross many parent nodes.

  Nodes are rendered in the same order as they are inserted into the graph.
  Nodes can be re-ordered within its siblings based on an arbitrary position
  index. A node with a similar or higher position value than its next immediate 
  sibling will be visited first.
*/

/*!
  SceneNode constructor. Initializes an independent node that is not part
  of any graph
 */
SceneNode::SceneNode()
    : _parent(0),
      _firstchild(0),
      _lastchild(0),
      _nextsibling(0),
      _previoussibling(0),
      _current_renderer(0),
      _position(INIT_POSITION),
      _children(0),
      _processed(false),
      _type(Root)
{
}

/*!
  SceneNode destructor. A destroyed node removes itself from the graph it is 
  part of
 */
SceneNode::~SceneNode()
{
    detach();
}

/*!
  Sets the position of this node within its siblings to \a index. A node with 
  a similar or higher position index than its next immediate sibling will be 
  visited first.
 */
void SceneNode::setPosition(unsigned int index )
{
    if (!parentNode() || (index == _position) || (_type == Effect))
        return;

    static bool init_set_pos = false;
    if (_nextsibling || _previoussibling) {
        for (SceneNode* current = parentNode()->_firstchild; current; 
             current = current->_nextsibling) {          
            if (index >= current->_position && current->_position != INIT_POSITION) {
                parentNode()->insertChildBefore(current, this);
                init_set_pos = true;
                break;
            } else if (current == parentNode()->_lastchild && init_set_pos)
                parentNode()->insertChildAfter(current, this);
        }
    } else 
        parentNode()->setPosition(index);
    
    _position = index;
}

/*!
  Adds \a child at the end of this node's children 
*/
void SceneNode::appendChild(SceneNode* child)
{
    child->_parent = this;
    _children++;

    if (_lastchild) 
        _lastchild->_nextsibling = child;
    else 
        _firstchild = child;
        
    child->_previoussibling = _lastchild;
    _lastchild = child;
}

/*!
  Adds \a child at the beginning of this node's children 
*/
void SceneNode::prependChild(SceneNode* child)
{
    child->_parent = this;
    _children++;
    
    if (_firstchild)
        _firstchild->_previoussibling = child;
    else
        _lastchild = child;
    
    child->_nextsibling = _firstchild;
    _firstchild = child;
}

/*!
  Insert \a child before a SceneNode \a before
*/
void SceneNode::insertChildBefore(SceneNode* before, SceneNode* child)
{
    if (before == child || before->_previoussibling == child)
        return; 
    
    child->_parent = this;

    SceneNode* beforeSibling = before->_previoussibling;
    if (beforeSibling)
        beforeSibling->_nextsibling = child;
    else
        _firstchild = child;

    SceneNode* childnext = child->_nextsibling;
    if (childnext)
        childnext->_previoussibling = child->_previoussibling;
    else
        _lastchild = child->_previoussibling;
    SceneNode* childprev = child->_previoussibling;
    if (childprev)
        childprev->_nextsibling = child->_nextsibling;
    else
        _firstchild = child->_nextsibling;

    before->_previoussibling = child;
    child->_nextsibling = before;
    child->_previoussibling = beforeSibling;
}

/*!
  Insert \a child after a SceneNode \a after
*/
void SceneNode::insertChildAfter(SceneNode* after, SceneNode* child)
{
    if (after == child || after->_nextsibling == child)
        return; 
    
    child->_parent = this;

    SceneNode* afterSibling = after->_nextsibling;
    if (afterSibling)
        afterSibling->_previoussibling = child;
    else
        _lastchild = child;

    SceneNode* childnext = child->_nextsibling;
    if (childnext)
        childnext->_previoussibling = child->_previoussibling;
    else
        _lastchild = child->_previoussibling;
    SceneNode* childprev = child->_previoussibling;
    if (childprev)
        childprev->_nextsibling = child->_nextsibling;
    else
        _firstchild = child->_nextsibling;

    after->_nextsibling = child;
    child->_nextsibling = afterSibling;
    child->_previoussibling = after;
}

/*! Detaches this node from the graph */
void SceneNode::detach()
{
    if (_parent && (_type != Effect)) {
        _parent->_children--;
        
        if (_nextsibling)
            _nextsibling->_previoussibling = _previoussibling;
        else
            _parent->_lastchild = _previoussibling;
        
        if (_previoussibling)
            _previoussibling->_nextsibling = _nextsibling;
        else
            _parent->_firstchild = _nextsibling;
    }
    
    _parent = 0;
    _nextsibling = 0;
    _previoussibling = 0;
}

/*! 
  Removes all reference of child nodes from this node. This node does not 
  delete its children.
*/
void SceneNode::clearChildren()
{
    _firstchild = 0;
    _lastchild = 0;
}

/*!
  \return The decision of the node traversal when the renderer encounters this
  node for every frame.

  Re-implement in a subclass to determine whether the renderer will proceed to
  the next node, skip the next node, or completely stop processing after this
  node.
 */
SceneRender::WalkNode SceneNode::processNode()
{
    /*NOOP*/
    return SceneRender::ProcessNext;
}

/*!
   \class Item2DInterface
   \brief Item2DInterface is the public interface for TransformNode operations
   to map dynamic 2D objects to the scenegraph.

   Item2DInterface is an interface used by a TransformNode to dynamically 
   set its properties based on an external source. Re-implement the pure virtual
   functions transform(), isVisible(), hasAlpha(), and opacity() to set,
   respectively, the transformation matrix, visibility, alpha channel, and 
   opacity of a TransformNode.
*/
Item2DInterface::Item2DInterface()
  : _fbo_container(false), 
    _blendfunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA){}

/*!
  \fn void Item2DInterface::setFboContainer(bool f);

  Sets whether the TransformNode assigned to this Item2DInterface is
  an fbo container node.
*/

/*!
  \fn bool Item2DInterface::fboContainer();
  
  \return Returns whether the TransformNode assigned to this Item2DInterface is
  an fbo container node
*/

/*!
  Sets the blending function of the renderer in blending operations for this
  interface used by a TransformNode.
  \a src Specifies how the red, green, blue, and alpha source blending 
  factors are computed.
  \dest Specifies how the red, green, blue, and alpha destination blending 
  factors are computed
 */
void Item2DInterface::setBlendFunc(GLenum src, GLenum dest)
{
    _blendfunc = qMakePair(src, dest);
}

/*!
  \class TransformNode
  \brief TransformNode represents a transformation node in the scenegraph

  TransformNode is a node that allows a transformation and other state changes
  to be applied to all its children

  TransformNode uses a 4x4 transformation matrix in 3D space to orient objects
  within the scenegraph. Use the Item2DInterface class to map 2D objects
  to a TransfomNode
*/

/*!
  TransformNode constructor. Creates a transformation node set to an initial
  transformation matrix \transform
 */
TransformNode::TransformNode(const QMatrix4x4& transform)
    : SceneNode(),
     _transform(transform),
     _in_fbo(false),
     _transform_handler(0)
{
    setNodeType(SceneNode::Transform);
}

/*!
  /return The decision of this transformation node in instructing the renderer
  whether to proceed traversing to the next immediate child or sibling nodes.
  
  The conditions are applied based on an implementation of an Item2DInterface 
  handler that is applied to this TransformNode
 */
SceneRender::WalkNode TransformNode::processNode()
{
    SceneRender* render = currentRenderer();
    if (render && _transform_handler) {
        if ((_transform_handler->fboContainer() && render->fboRender()) ||
            (!_in_fbo && render->fboRender()) ||
            (_in_fbo && !render->fboRender())) 
            {
                return SceneRender::SkipNext;
            }
    }
        
    if (_transform_handler) {
        if (!_in_fbo && !_transform_handler->isVisible()) {
            return SceneRender::SkipNext;
        }
        QTransform t = _transform_handler->transform();
        bool ident = false;
        if (qRound(t.m31()) == 0 &&
            qRound(t.m32()) == 0 &&
            !t.isIdentity()) {
            t.reset();
            ident = true;
        }
        _transform = t;
        GLfloat z = - (int) nodePosition();
        _transform.translate(0, 0, z);
        if (render) {
            render->_current_alpha   = _transform_handler->hasAlpha();
            render->_current_opacity = _transform_handler->opacity();
            render->_current_blendfunc = _transform_handler->blendFunc();
        }
    }
    
    if (render)
        render->_current_transform = _transform;
    
    return SceneRender::ProcessNext;
}

/*!
  \class GeometryNode
  \brief GeometryNode encapsulates geometry and appearance data to describe
  a viewable object.

  GeometryNode represents an object that can be viewed on the scenegraph.
  It describes a texture that is applied to a quad that is implemented in local
  coordinates.  A GeometryNode's appearance can be dynamically modified by 
  adding an EffectNode child to it.

*/
GeometryNode::GeometryNode(const QRectF& rect)
    : SceneNode(),
     _geometry(rect),
     _texture_id(0),
     _shader_id(0),
     _opacity(1.0),
     _has_alpha(false),
     _visible(true),
     _inherit_transform(true),
     _effect_processed(false),
     _blendfunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA),
     _uniformhandler(0)
     
{
    setNodeType(SceneNode::Geometry);
}

/*!
  /return The decision of this GeometryNode in instructing the renderer
  whether to proceed traversing to the next immediate child or sibling nodes.
  
  This function implements a visibility test based on a sweep line algorithm 
  variant to determine whether this GeometryNode is visible with respects to 
  other GeometryNodes.
  */
SceneRender::WalkNode GeometryNode::processNode()
{
    static QRectF world_bounding = QApplication::desktop()->screenGeometry();
    SceneRender* r = currentRenderer();
    
    if (r) {    
        bool topnode = (r->_process_depth == 0);
        QRectF current_rect;
        if (_inherit_transform) {
            current_rect = r->currentTransform().mapRect(geometry());
            _opacity     = r->_current_opacity;
            _blendfunc   = r->_current_blendfunc;
        } else {
            current_rect = geometry();
            _opacity     = 1.0;
            r->_current_alpha = false;
            _blendfunc   = qMakePair((GLenum)GL_SRC_ALPHA, 
                                     (GLenum)GL_ONE_MINUS_SRC_ALPHA);
        }
        
        if (topnode)
            r->_current_bounding = current_rect;
        // Check first if this node is worth processing at all
        if (world_bounding.intersects(current_rect))
            r->_current_bounding |= current_rect;
        else
            return SceneRender::SkipNext;
        // Check if closest opaque nodes cover this node
        if (!r->rectVisible(world_bounding.intersected(current_rect)))
            return SceneRender::SkipNext;
        bool alpha = _has_alpha || r->_current_alpha;
        // viewport is fully covered, frame is done after this node
        if (world_bounding == r->_current_bounding && !alpha)
            return SceneRender::Stop;
        // got an alpha mesh
        if (alpha) {
            r->_alpha_stack.push_back(qMakePair(r->currentTransform(),this));
            return SceneRender::SkipNext;
        } 
        
        r->_visible_rects.push_back(current_rect);
    }
    setProcessed(true);
    return SceneRender::ProcessNext;
}

/*!
  Sets the uniforms provided in the QGLShaderProgram \a program for this node 
  if it uses a custom shader
 */
void GeometryNode::setUniforms(QGLShaderProgram* program)
{
    if (uniformHandler())
        uniformHandler()->setUniforms(program);
}

/*!
  Sets texture coordinates for this GeometryNode to \a coords
 */
void GeometryNode::setTextureCoords(const TextureCoords& coords) 
{ 
    _texcoords = coords; 

}

/*!
  \return The texture coordinates used by this GeometryNode
 */
const TextureCoords& GeometryNode::texcoords() const 
{ 
    return _texcoords; 
}

/*!
  \class EffectInterface
  \brief EffectInterface is the public interface for GeometryNode shader
  effects and properties
  
  EffectInterface provides a way to dynamically modify a GeometryNode's shader
  effects and properties. Re-implement the pure virtual functions 
  currentNodeProcessed() and setUniforms() to modify properties
  and set the uniforms, respectively, of the currently processed GeometryNode
*/

/*! 
  \class EffectNode
  \brief EffectNode is specialized node that can have multiple parent 
  geometry nodes 

  EffectNode provides a way to use custom vertex and fragment shader effects
  for a GeometryNode. Use setEffectHandler() to set an effect handler that can 
  manipulate its shader program values and directly modify 
  GeometryNode properties.

  In addition, it is a container for child GeometryNode
  objects to allow rendering of additional content with the main GeometryNode.
*/
EffectNode::EffectNode()
    :SceneNode(),
     effect(0)
{
    setNodeType(SceneNode::Effect);
}

/*!
  \return The decision of EffectNode traversal
 */
SceneRender::WalkNode EffectNode::processNode()
{
    SceneRender* render = currentRenderer();
    if (!render || !render->currentParent() || !effect)
        return SceneRender::ProcessNext;
    
    Q_ASSERT(render->currentParent()->nodeType() == SceneNode::Geometry);
    GeometryNode* g = static_cast<GeometryNode*>(render->currentParent());
    g->setProcessedByEffect(true);
    effect->currentNodeProcessed(g);
    
    return SceneRender::ProcessNext;
}

/*!
  \return Sets the position of this effect node
 */
void EffectNode::setPosition(unsigned int idx)
{
    // set position for child items as well
    for (SceneNode* current = _firstchild; current; 
         current = current->_nextsibling) 
        current->_position = idx;

    _position = idx;
}

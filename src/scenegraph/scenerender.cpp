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

#include "scenerender.h"
#include "scenenode.h"

#include "texturepixmapshaders.h"

/*!
  \class SceneRender is the render object of the scenegraph that draws the
  scene while traversing the nodes
  
  SceneRender starts from a root node then traverses to TransformNodes where
  it will query the rendering states such as opacity and the transformation
  matrix, among others, and apply these to subsequent rendering operations in
  GeometryNodes. The renderer optimally re-orders rendering front to back for
  opaque items and back to front for translucent nodes. Opaque nodes are 
  processed first and then translucent nodes. The renderer uses the 4x4 
  transformation matrix, QMatrix4x4, to transform objects in 3D space.

  A scene is rendered by using the renderScene() function. SceneRender can
  render directly to the framebuffer or to an FBO.
 */

//#define CONSOLE_DEBUG

static const GLuint D_VERTEX_COORDS = 0;
static const GLuint D_TEXTURE_COORDS = 1;

static void bindAttribLocation(QGLShaderProgram *p, const char *attrib, int location)
{
    p->bindAttributeLocation(attrib, location);
}

class SceneRenderPrivate: public QObject
{
public:
    class CachedShaderProgram: public QGLShaderProgram
    {
    public:
        CachedShaderProgram(const QGLContext* context, QObject* parent);
        void setWorldMatrix(const QMatrix4x4& m, bool forced = false);
        void setTexture(GLuint t);
        void setOpacity(GLfloat o);
    
    private:
        QMatrix4x4 worldMatrix;
        GLfloat opacity;
        GLuint texture;
    };

    /*
     * This is the default set of shaders. Use MCompositeWindowShaderEffect 
     * class to add more specialzed shader effects 
     */
    enum ShaderType {
        NormalShader = 0,
        OpacityShader,
        ShaderTotal
    };

    SceneRenderPrivate(QGLWidget *glwidget);
    void initVertices(QGLWidget *glwidget);
    void updateVertices(const QMatrix4x4 &t, ShaderType type);    
    void updateVertices(const QMatrix4x4 &t, GLuint customShaderId);
    
    GLuint installPixelShader(const QByteArray& fragment, const QByteArray& vertex);
    
private:
    static CachedShaderProgram *shader[ShaderTotal];
    QHash<GLuint, CachedShaderProgram *> customShadersById;
    QHash<QByteArray, CachedShaderProgram *> customShadersByCode;
    const QGLContext* glcontext;    
    
    QMatrix4x4 projMatrix;
    CachedShaderProgram *currentShader;
    QGLWidget* glw;
    int width;
    int height;

    friend class SceneRender;
};

SceneRenderPrivate::CachedShaderProgram* SceneRenderPrivate::shader[ShaderTotal];
SceneRenderPrivate* SceneRender::d = 0;

SceneRenderPrivate::SceneRenderPrivate(QGLWidget *glwidget)
    :QObject(glwidget),
     glcontext(glwidget->context()),
     currentShader(0),
     glw(glwidget)
{
    QByteArray vshader = QLatin1String(TexpVertShaderSource).latin1();
    
    CachedShaderProgram *normalShader = new CachedShaderProgram(glwidget->context(),
                                                                this);
    if (!normalShader->addShaderFromSourceCode(QGLShader::Vertex, vshader))
        qWarning("vertex shader failed to compile");
    if (!normalShader->addShaderFromSourceCode(QGLShader::Fragment,
                                               QLatin1String(TexpFragShaderSource)))
        qWarning("normal fragment shader failed to compile");
    shader[NormalShader] = normalShader;
    
    CachedShaderProgram* opacityShader = new CachedShaderProgram(glwidget->context(),
                                                                this);
    if (!opacityShader->addShaderFromSourceCode(QGLShader::Vertex, vshader))
        qWarning("alpha vertex shader failed to compile");
    if (!opacityShader->addShaderFromSourceCode(QGLShader::Fragment,
                                              QLatin1String(TexpOpacityFragShaderSource)))
        qWarning("alpha fragment shader failed to compile");
    
    shader[OpacityShader] = opacityShader;
    
    bindAttribLocation(normalShader, "inputVertex", D_VERTEX_COORDS);
    bindAttribLocation(normalShader, "textureCoord", D_TEXTURE_COORDS);
    bindAttribLocation(opacityShader, "inputVertex", D_VERTEX_COORDS);
    bindAttribLocation(opacityShader, "textureCoord", D_TEXTURE_COORDS);
    
    normalShader->link();
    opacityShader->link();
}

void SceneRenderPrivate::initVertices(QGLWidget *glwidget)
{
    width = glwidget->width();
    height = glwidget->height();
    glViewport(0, 0, width, height);
    
    projMatrix = QMatrix4x4(2.0 / width, 0.0,
                            0.0        ,-1.0,
                            0.0        ,-2.0 / height,
                            0.0        , 1.0,
                            0.0        , 0.0,
                            2.0 /height, 0.0,
                            0.0        , 0.0,
                            0.0        , 1.0);
    
    for (int i = 0; i < ShaderTotal; i++) {
        shader[i]->bind();
        shader[i]->setUniformValue("matProj", projMatrix);
    }
}

void SceneRenderPrivate::updateVertices(const QMatrix4x4 &t, ShaderType type) 
{        
    bool current_changed = false;
    if (shader[type] != currentShader) {
        currentShader = shader[type];
        current_changed = true;
    }
    
    if (!currentShader->bind())
        qWarning() << __func__ << "failed to bind shader program";
    // When active program changes, the uniforms must be updated
    currentShader->setWorldMatrix(t, current_changed);
}

void SceneRenderPrivate::updateVertices(const QMatrix4x4 &t, GLuint customShaderId) 
{                
    if (!customShaderId)
        return;
    CachedShaderProgram* frag = customShadersById.value(customShaderId, 0);
    if (!frag)
        currentShader = shader[NormalShader];
    else
        currentShader = frag;
    if (!currentShader->bind())
        qWarning() << __func__ << "failed to bind shader program";
    currentShader->setWorldMatrix(t);
}

GLuint SceneRenderPrivate::installPixelShader(const QByteArray& fragment, 
                                              const QByteArray& vertex)
{
    QHash<QByteArray, CachedShaderProgram *>::iterator it = customShadersByCode.find(fragment);
    if (it != customShadersByCode.end())  {
        return it.value()->programId();
    }
    
    QByteArray source = fragment;
    source.append(TexpCustomShaderSource);
    CachedShaderProgram *p = new CachedShaderProgram(glcontext, this);
    
    QByteArray sharedvshader = QLatin1String(TexpVertShaderSource).latin1();
    QByteArray vshader = vertex.isNull()?  sharedvshader: vertex;
    if (!p->addShaderFromSourceCode(QGLShader::Vertex, vshader))
        qWarning("vertex shader failed to compile");
    
    if (!p->addShaderFromSourceCode(QGLShader::Fragment,
                                    QLatin1String(source)))
        qWarning("custom fragment shader failed to compile");
    
    bindAttribLocation(p, "inputVertex", D_VERTEX_COORDS);
    bindAttribLocation(p, "textureCoord", D_TEXTURE_COORDS);
    
    if (p->link()) {
        if (p->bind())
            p->setUniformValue("matProj", projMatrix);
        customShadersByCode[fragment] = p;
        customShadersById[p->programId()] = p;
        return p->programId();
    } 
    
    qWarning() << "failed installing custom fragment shader:"
               << p->log();
    p->deleteLater();
    
    return 0;
}

SceneRenderPrivate::CachedShaderProgram::CachedShaderProgram(const QGLContext* c, 
                                                             QObject* parent)
    :QGLShaderProgram(c, parent)
{
    opacity = -1;
}

void SceneRenderPrivate::CachedShaderProgram::setWorldMatrix(const QMatrix4x4& m,
                                                             bool forced) 
{
    static bool init = true;
    if (init || m != worldMatrix || forced) {
        worldMatrix = m;
        setUniformValue("matWorld", worldMatrix);
        init = false;
    }
}

void SceneRenderPrivate::CachedShaderProgram::setTexture(GLuint t) 
{
    if (t != texture) {
        setUniformValue("texture", t);
        texture = t;
    }
}

void SceneRenderPrivate::CachedShaderProgram::setOpacity(GLfloat o) 
{
    if (o != opacity) {
        setUniformValue("opacity", o);
        opacity = o;
    }
}

/*!
  SceneRender constructor. Initializes rendering states to default values
 */
SceneRender::SceneRender()
    : _current_parent(0),
      _process_depth(0),
      _fbo_render(false),
      _current_alpha(false),
      _clearscene(false),
      _current_opacity(1.0),
      _current_blendfunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
#ifdef DEBUG_SCENEGRAPH
     ,_render_debug(0)
#endif
{
}

/*!
  Initializes the render to use QGLWidget \a widget
*/
void SceneRender::init(QGLWidget* widget)
{
    if (!widget->context() || !widget->isValid()) {
        qWarning("%s: no current context", __func__);
        return;
    }
    widget->makeCurrent();
    if (!d) {
        d = new SceneRenderPrivate(widget);
        d->initVertices(widget);
    }
}

/*!
  Installs a \a vertex and \a fragment shader to the renderer
  
  \return Returns the shader id of the custom vertex and shader function
 */
GLuint SceneRender::installPixelShader(const QByteArray& fragment, const QByteArray& vertex)
{   
    if (!d) {
        qWarning("%s: GL widget not initialized", __func__);
        return 0;
    }
    d->glw->makeCurrent();
    return d->installPixelShader(fragment, vertex);
}

SceneRender::WalkNode SceneRender::renderScene(SceneNode* root)
{
    static SceneRender::WalkNode status = SceneRender::ProcessNext;
    _current_parent = root;
    
    if (_process_depth == 0) {
        if (!_fbo_render && _clearscene) {
            glClearColor(0.0, 0.0, 0.0, 0.0);
            glClear(GL_COLOR_BUFFER_BIT);
            return SceneRender::Stop;
        }

        glFrontFace(GL_CW);
        glCullFace(GL_FRONT);
        glEnable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);
    }
    
    for (SceneNode* current = root->_firstchild; current; 
         current = current->_nextsibling) {
        
        if (status == SceneRender::Stop)
            break;
        current->_current_renderer = this;
        current->setProcessed(false);

        // explicitly process child EffectNodes first if we have one
        if (current->_firstchild && 
            current->_firstchild->nodeType() == SceneNode::Effect) {
            _current_parent = current;
            current->_firstchild->_current_renderer = this;
            current->_firstchild->processNode();
            renderScene(current->_firstchild);
        }
        if (current->nodeType() != SceneNode::Effect) {
            if ((status = current->processNode()) == SceneRender::SkipNext)
                continue;
        }
        
        if (current->nodeType() == SceneNode::Geometry) {
            renderGeometry((GeometryNode*) current, _current_transform);
            ++_process_depth;
        }
        
        if (status == SceneRender::Stop)
            break;   
            
        // child nodes 
        if (current->nodeType() != SceneNode::Effect) 
            status = renderScene(current); 
    }
        
    if (root->nodeType() == SceneNode::Root) {
        // end of the line
        // process visible translucent items
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        
        while (!_alpha_stack.isEmpty()) {
            AlphaMesh m = _alpha_stack.last();
            
            if (m.second->nodeType() == SceneNode::Geometry) {
                GeometryNode* g = (GeometryNode*) m.second;
                static BlendFunc frame_blendf(0,0);
                if (g->blendFunc() != frame_blendf) {
                    frame_blendf = g->blendFunc();
                    glBlendFunc(frame_blendf.first, frame_blendf.second);
                }
                renderGeometry(g, m.first);   
                ++_process_depth;
            }
            _alpha_stack.pop_back();
        }
        glDisable(GL_BLEND);
        
#if (defined DEBUG_SCENEGRAPH && defined CONSOLE_DEBUG)
        qDebug("--- end frame ---");
#endif
        _visible_rects.clear();
        _process_depth = 0;
        _current_parent = 0;
        status = SceneRender::ProcessNext;
    }
    return status;
}

#ifdef DEBUG_SCENEGRAPH
static void render_debug(const char* location, SceneRender* s, 
                         GeometryNode* node, const QMatrix4x4& transform)
{
#ifdef CONSOLE_DEBUG
    QByteArray loc = location;
    loc = node->hasAlpha() ? loc + " Alpha" : loc; 
    qDebug() << loc.data()
             << (s->fboRender()? "(in FBO)" : "(in framebuf)")
             << "[geom:" << node->geometry()
             << "] [trans.geom:" 
             <<  transform.mapRect(node->geometry())
             << "] [hw Z:" << transform.data()[14]
             << "] [shader:" << node->shaderId()
             << "] [texture" << node->texture() << "]";
#endif
    static GLuint tex = 0;
    tex = node->texture();
    if (s->debugFilter())
        s->debugFilter()(static_cast<void*>(&tex));
}
#endif

void SceneRender::renderGeometry(GeometryNode* node, QMatrix4x4& transform)
{
    if (!QGLContext::currentContext())
        return;
    
    // This node has a child EffectNode child and it hasn't been processed yet
    if (node->uniformHandler() && !node->effectProcessed())
        return;
    
    if (_process_depth == 0 && !_fbo_render) {
        glClearColor(0.0, 0.0, 0.0, 0.0);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    static QMatrix4x4 ident_t;
    glBindTexture(GL_TEXTURE_2D, node->texture());
    
    if (node->shaderId() == 0) {
        SceneRenderPrivate::ShaderType t = (node->opacity() < 1.0f) ?
            SceneRenderPrivate::OpacityShader:SceneRenderPrivate::NormalShader;
        d->updateVertices(node->inheritTransform() ? transform : ident_t, t);
    } else
        d->updateVertices(node->inheritTransform() ? transform : ident_t,
                              node->shaderId());
    GLfloat vertexCoords[] = {
        node->geometry().left(),  node->geometry().top(),    0,
        node->geometry().left(),  node->geometry().bottom(), 0,
        node->geometry().right(), node->geometry().bottom(), 0,
        node->geometry().right(), node->geometry().top(),    0,
    };
    
    static GLfloat texCoordsInv[8];
    const_cast<TextureCoords&>(node->texcoords()).getTexCoords(texCoordsInv);
    
    glEnableVertexAttribArray(D_VERTEX_COORDS);
    glEnableVertexAttribArray(D_TEXTURE_COORDS);
    glVertexAttribPointer(D_VERTEX_COORDS, 3, GL_FLOAT, GL_FALSE, 0, vertexCoords);
    glVertexAttribPointer(D_TEXTURE_COORDS, 2, GL_FLOAT, GL_FALSE, 0, texCoordsInv);
    // uniforms
    node->setUniforms(d->currentShader);
    d->currentShader->setOpacity((GLfloat) node->opacity());
    d->currentShader->setTexture(0);
    
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    
    glDisableVertexAttribArray(D_VERTEX_COORDS);
    glDisableVertexAttribArray(D_TEXTURE_COORDS);
    
#ifdef DEBUG_SCENEGRAPH
    render_debug(__func__, this, node, transform);
#endif
}

bool SceneRender::rectVisible(const QRectF& rect)
{
    for (int i = _visible_rects.size() - 1; i >= 0; --i) 
        if (_visible_rects[i].contains(rect))
            return false;
    return true;
}

bool SceneRender::intersectsOrSameEdge(const QRectF& r1, const QRectF& r2)
{
    return (r1.intersects(r2)
            || r1.top() == r2.bottom()
            || r1.left() == r2.right()
            || r1.right() == r2.left()
            || r1.bottom() == r2.top());
}

/*!
 * Returns the rectangle of the most recently rendered GeometryNode that shares
 * an edge or intersects with the given \rect
 */
const QRectF SceneRender::intersectingVisible(const QRectF& rect)
{
    for (int i = _visible_rects.size() - 1; i >= 0; --i) {
        QRectF c = _visible_rects[i];
        if (intersectsOrSameEdge(rect,c))
            return c;
    }
    return QRectF();
}

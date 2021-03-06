/****************************************************************************
**
** Copyright (C) 2019 Toradex Ag
** Contact: https://www.toradex.com/locations
**
** This file is part of the Toradex and XNOR people detection sample.
**
** $QT_BEGIN_LICENSE:BSD$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** BSD License Usage
** Alternatively, you may use this file under the terms of the BSD license
** as follows:
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * Neither the name of Toradex Ag nor the names of its
**     contributors may be used to endorse or promote products derived
**     from this software without specific prior written permission.
**
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "framegrabber.h"
#include <stdlib.h>

QVideoFilterRunnable *FrameGrabber::createFilterRunnable()
{
    FilterRunnable *filter = new FilterRunnable(this);

    return filter;
}

FilterRunnable::FilterRunnable(FrameGrabber *frameGrabber)
    : m_frameGrabber(frameGrabber)
{
    //qDebug() << "FROM filter runnable constructor THREAD:" << QThread::currentThreadId();
}

QVideoFrame FilterRunnable::run(QVideoFrame *input, const QVideoSurfaceFormat &surfaceFormat, RunFlags flags){
    Q_UNUSED(surfaceFormat);
    Q_UNUSED(flags);

    FilterResult *r = new FilterResult;

    input->map(QAbstractVideoBuffer::ReadOnly);

    QByteArray * imgBytes = new QByteArray((const char*)input->bits(), input->mappedBytes());

    // pass byte array to XNOR.ai class

    if(!imgBytes->isEmpty() && !imgBytes->isNull()){
            if(loadModel){

                /*** LOAD MODEL BUILT IN ***/

                xerror = xnor_model_load_built_in("", NULL, &xmodel);

                if(xerror != NULL){
                    qDebug() << "xnor_model_load_built_in return error\n"
                             << xnor_error_get_description(xerror);
                    return QVideoFrame();
                }
                loadModel = false;
            }
            timer.restart();


            /*** CREATE A HANDLE TO IMAGE INPUT ***/

            switch (input->pixelFormat()) {
            case QVideoFrame::Format_YUV420P:{
                // splits into planes
                uint8_t *y = static_cast<uint8_t *>(input->bits(1)); // each one
                uint8_t *u = static_cast<uint8_t *>(input->bits(2)); // of the
                uint8_t *v = static_cast<uint8_t *>(input->bits(3)); // planes
                xerror = xnor_input_create_yuv420p_image(input->width(),input->height(),y, u, v, &xinput);
                break;
            }
            case QVideoFrame::Format_RGB32:{
                xerror = xnor_input_create_rgb_image(input->width(), input->height(),
                                                     (uint8_t *)imgBytes->data(), &xinput);
                break;
            }
            case QVideoFrame::Format_YUYV:{ // YUYV IS YUV422
               xerror = xnor_input_create_yuv422_image(input->width(), input->height(), (
                                                           uint8_t *)imgBytes->data(), &xinput);
                break;
            }
            default:
                break;
            }

            if(xerror != NULL){
                qDebug() << "xnor_input_create_" << input->pixelFormat() << "image error!\n"
                         << xnor_error_get_description(xerror);
                r->m_error = QString("xnor_input_create_").append(input->pixelFormat()).append("image error!");
                return QVideoFrame();
            }

            /*** EVALUATES THE GIVEN MODEL ON THE GIVEN INPUT  ***/

            xerror = xnor_model_evaluate(xmodel, xinput, NULL, &xresult);

            if(xerror != NULL){
                qDebug() << "xnor_model_evaluate error!\n"
                         << xnor_error_get_description(xerror);
                r->m_error = QString("xnor_model_evaluate error!\n").append(xnor_error_get_description(xerror));
                return QVideoFrame();
            }

            /*** GET THE TYPE OF AN EVALUATION RESULT ***/

            xtype = xnor_evaluation_result_get_type(xresult);

            switch (xtype) {
            case 0:{
                //qDebug() << "Unknown result type";
                break;
                }
            case 1:{
                //qDebug() << "Bounding Boxes result type";


                out_size = xnor_evaluation_result_get_bounding_boxes(xresult, bbox, MAX_OUT_SIZE);

                xnor_evaluation_result_free(xresult);

                fpsList.append(1000/timer.elapsed());
                r->m_fps = mediumFPS();

                r->m_deltaT = timer.elapsed();
                r->m_fpsAvg = 1000*frameCount/timerAvg.elapsed();
                emit m_frameGrabber->finished(r);

                if(out_size < MAX_OUT_SIZE){
                    for(int i=0; i<out_size; i++){
                        r->m_bboxes.append(QRect(bbox[i].rectangle.x*(input->width()), bbox[i].rectangle.y*(input->height()),
                                                 bbox[i].rectangle.width*(input->width()), bbox[i].rectangle.height*input->height()));
                        r->m_class_ids.append(bbox[i].class_label.class_id);
                        r->m_confidence.append(bbox[i].class_label.confidence*100);
                        r->m_labels.append(bbox[i].class_label.label);
                    }
                }

                break;
                }
            case 2:{
                //qDebug() << "Class Labels result type";
                break;
                }
            }
    } else {
        qDebug() << input->isValid();
        qDebug() << input->isReadable();
        qDebug() << input->isMapped();
        return QVideoFrame();
    }

    input->unmap();

    // free imgBytes
    delete imgBytes;
    imgBytes = NULL;

    // free xnor
    xnor_error_free(xerror);
    xnor_input_free(xinput);

    return QVideoFrame();
}

int FilterRunnable::mediumFPS(){
    int medFPS = 0;
    if(fpsList.size() > MAX_OUT_SIZE){
        fpsList.removeFirst();
    }
    for(int i=0; i<fpsList.size(); i++){
        medFPS += fpsList.at(i);
    }
    return medFPS/fpsList.size();
}

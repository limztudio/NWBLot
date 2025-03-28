// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Timer g_testTimer = timerNow();


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Frame::startup(){
    if(!m_graphics.init(data<__hidden_frame::FrameData>().width(), data<__hidden_frame::FrameData>().height()))
        return false;

    return true;
}
void Frame::cleanup(){
    m_graphics.destroy();
}
bool Frame::update(float delta){
    {
		auto now = timerNow();
		if (durationInSeconds<float>(now, g_testTimer) > 0.5f)
		{
			g_testTimer = now;
			NWB_LOGGER_INFO(NWB_TEXT("delta: {}"), delta);
		}
    }

    return true;
}
bool Frame::render(){
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#IF (MODULE_cPVR) �Ƶ�ǰһĿ¼

    LIST (APPEND IPTV_CXX_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/JseHWPVR.cpp
        ${CMAKE_CURRENT_LIST_DIR}/JseHWSchedule.cpp
        ${CMAKE_CURRENT_LIST_DIR}/JseHWRecordFiles.cpp
        #${CMAKE_CURRENT_LIST_DIR}/JseHWLocalTimeshift.cpp
        )
  
#ENDIF (MODULE_cPVR)

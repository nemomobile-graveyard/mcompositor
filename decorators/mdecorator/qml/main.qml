import QtQuick 1.2
import org.nemomobile.mdecorator 0.1

Item {
    width: initialSize.width
    height: initialSize.height
    opacity: decoratorWindow.windowVisible ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 250 } }

    Rectangle {
        anchors.fill: parent
        color: "black"
        opacity: 0.6
    }

    Item {
        width: (decoratorWindow.orientationAngle % 180 == 90) ? initialSize.height : initialSize.width
        height: (decoratorWindow.orientationAngle % 180 == 90) ? initialSize.width : initialSize.height
        transform: Rotation {
            origin.x: { switch(decoratorWindow.orientationAngle) {
                      case 270:
                          return initialSize.height / 2
                      case 180:
                      case 90:
                          return initialSize.width / 2
                      default:
                          return 0
                      } }
            origin.y: { switch(decoratorWindow.orientationAngle) {
                case 270:
                case 180:
                    return initialSize.height / 2
                case 90:
                    return initialSize.width / 2
                default:
                    return 0
                } }
            angle: decoratorWindow.orientationAngle == 0 ? 0 : -360 + decoratorWindow.orientationAngle
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            y: parent.height / 10
            width: parent.width / 10 * 9
            height: parent.height / 5
            font.pixelSize: 36
            color: "white"
                  //% "%1 is not responding."
            text: qsTrId("qtn_reco_app_not_responding").arg(decoratorWindow.windowTitle) + " " +
                  //% "Do you want to close it?"
                  qsTrId("qtn_reco_close_app_question")
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        MouseArea {
            anchors.horizontalCenter: parent.horizontalCenter
            y: parent.height / 10 * 4
            width: parent.width / 2
            height: parent.height / 5
            onClicked: decoratorWindow.closeApplication()
            Rectangle {
                anchors.fill: parent
                border {
                    color: "white"
                    width: 2
                }
                color: "black"
                radius: 5

                Text {
                    anchors.fill: parent
                    font.pixelSize: 36
                    color: "white"
                    //% "Yes"
                    text: qsTrId("qtn_comm_command_yes")
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        MouseArea {
            anchors.horizontalCenter: parent.horizontalCenter
            y: parent.height / 10 * 7
            width: parent.width / 2
            height: parent.height / 5
            onClicked: decoratorWindow.doNotCloseApplication()
            Rectangle {
                anchors.fill: parent
                border {
                    color: "white"
                    width: 2
                }
                color: "black"
                radius: 5

                Text {
                    anchors.fill: parent
                    font.pixelSize: 36
                    color: "white"
                    //% "No"
                    text: qsTrId("qtn_comm_command_no")
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}

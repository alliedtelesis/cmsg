pipeline {
    agent { label 'maker' }
    options {
        disableConcurrentBuilds()
        timeout(time: 30)
        buildDiscarder(logRotator(numToKeepStr: '20'))
    }

    environment {
        PATH = "/home/maker/scripts:/bin:/usr/bin"
    }

    stages {
        stage('Fetch') {
            steps {
                sh "test -d awplus || git clone ssh://git.atlnz.lc/data/git/awplus.git"
                sh "git -C awplus pull"
                dir ("awplus") {
                    sh "./scripts/package done cmsg"
                    sh "./scripts/package edit cmsg"
                    sh "git -C source/cmsg fetch ${GERRIT_SCHEME}://maker@${GERRIT_HOST}:${GERRIT_PORT}/${GERRIT_PROJECT} ${GERRIT_REFSPEC}"
                    sh "git -C source/cmsg -c advice.detachedHead=false checkout FETCH_HEAD"
                }
            }
        }
        stage('Build') {
            steps {
                dir ("awplus") {
                    sh "buildsys test32 -- package-to-test=cmsg/proxy package-to-test-is-subarch=yes"
                }
            }
        }
        stage("Test") {
            steps {
                dir ("awplus") {
                    sh "./scripts/run-unit-tests.py --junit_format -t '^cmsg/.*'"
                }
            }
        }
    }
    post {
        always {
            junit allowEmptyResults: true, testResults: '**/reports/*.xml'
        }
    }
}

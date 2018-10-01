pipeline {
    agent any

    environment {
        PROJECT = 'sds_metrics'
        CONAN_CHANNEL = 'testing'
        CONAN_USER = 'sds'
        CONAN_PASS = credentials('CONAN_PASS')
    }

    stages {
        stage('Get Version') {
            steps {
                script {
                    TAG = sh(script: "grep -m 1 'version =' conanfile.py | awk '{print \$3}' | tr -d '\n' | tr -d '\"'", returnStdout: true)
                }
            }
        }

        stage('Build') {
            steps {
                sh "docker build --rm --build-arg CONAN_USER=${CONAN_USER} --build-arg CONAN_PASS=${CONAN_PASS} --build-arg CONAN_CHANNEL=${CONAN_CHANNEL} -t ${PROJECT}-${TAG} ."
            }
        }

        stage('Test') {
            steps {
                sh "docker images | grep ${PROJECT}_coverage && docker rm -f ${PROJECT}_coverage"
                sh "docker create --name ${PROJECT}_coverage ${PROJECT}-${TAG}"
                sh "docker cp ${PROJECT}_coverage:/output/coverage.xml coverage.xml"
                sh "docker rm -f ${PROJECT}_coverage"
                cobertura autoUpdateHealth: false, autoUpdateStability: false, coberturaReportFile: 'coverage.xml', conditionalCoverageTargets: '0, 0, 0', fileCoverageTargets: '0, 0, 0', lineCoverageTargets: '0, 0, 0', maxNumberOfBuilds: 0, sourceEncoding: 'ASCII', zoomCoverageChart: false
            }
        }

        stage('Deploy') {
            when {
                branch "${CONAN_CHANNEL}/*"
            }
            steps {
                sh "docker run --rm ${PROJECT}-${TAG}"
                slackSend channel: '#conan-pkgs', message: "*${PROJECT}/${TAG}@${CONAN_USER}/${CONAN_CHANNEL}* has been uploaded to conan repo."
            }
        }
    }

    post {
        always {
            sh "docker rmi -f ${PROJECT}-${TAG}"
        }
    }
}
